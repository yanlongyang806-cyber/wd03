#define GENESIS_ALLOW_OLD_HEADERS
#include "WorldEditorUI.h"

#ifndef NO_EDITORS
#include "aiStructCommon.h"
#include "ChoiceTable_common.h"
#include "CurveEditor.h"
#include "EString.h"
#include "EditLibGizmosToolbar.h"
#include "EditLibUIUtil.h"
#include "EditorManagerUI.h"
#include "EditorManagerUtils.h"
#include "EditorObjectMenus.h"
#include "EditorPrefs.h"
#include "EditorPreviewWindow.h"
#include "encounter_common.h"
#include "eventeditor.h"
#include "enclayereditor.h"
#include "Expression.h"
#include "GfxClipper.h"
#include "GfxEditorIncludes.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxTextureTools.h"
#include "GlobalEnums_h_ast.h"
#include "InputLib.h"
#include "inputText.h"
#include "MapDescription.h"
#include "GenesisMapDescriptionEditor.h"
#include "MapDescription_h_ast.h"
#include "Materials.h"
#include "ObjectLibrary.h"
#include "partition_enums.h"
#include "ProgressOverlay.h"
#include "StringCache.h"
#include "UnitSpec.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorGizmos.h"
#include "WorldEditorMenus.h"
#include "WorldEditorNotes.h"
#include "WorldEditorOperations.h"
#include "WorldEditorOptions.h"
#include "WorldEditorPrivate.h"
#include "WorldEditorUtil.h"
#include "WorldLib.h"
#include "rewardCommon.h"
#include "groupdbmodify.h"
#include "crypt.h"
#include "inputLib.h"
#include "tokenstore.h"
#include "wlEditorIncludes.h"
#include "wlEncounter.h"
#include "wlGenesis.h"
#include "wlPhysicalProperties.h"
#include "wlTerrainSource.h"
#include "wlTime.h"
#include "bounds.h"
#include "Sound_common.h"
#include "Fileutil.h"
#include "Fileutil2.h"
#include"gimmeDLLWrapper.h"
#include "sysutil.h"
#include "StringUtil.h"
#include "qsortG.h"
#include "CharacterClass.h"

#include "WorldEditorUI_h_ast.h"
#include "WorldEditorOptions_h_ast.h"
#include "EditorObject_h_ast.h"

#include "ResourceSystem_Internal.h"
#include "logging.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "autogen/PvPGameCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* CONSTANTS AND MACROS
********************/
#define NO_NAMESPACE_STR "None"
#define WLE_TRACKER_TREE_NODE_HEIGHT 15
#define radToDeg(rad) (rad * 180 / PI)
#define degToRad(deg) (deg * PI / 180)
static StaticDefineInt* WorldVariableDefaultValueTypeSansVariablesEnum = NULL;

/********************
* FORWARD DECLARATIONS
********************/
static void refreshTintWidgets(void);
static void wleUITrackerNodeSelect(UITree *trackerTree, void *stuff);
static void wleUITrackerTreeDisplayModeRefresh(void);
static void wleGenesisUIEditCompleteCB(MapDescEditDoc *pDoc, GenesisMapDescription *map_desc, bool seed_layout, bool seed_detail);


/********************
* GLOBAL VARIABLES
********************/
WorldEditorUIState *editorUIState;

int gEditorTeamSize = 1;
EncounterDifficulty g_eEditorDifficulty = 0;

/********************
* UTIL
********************/
/******
* This prints a message indicating the selection is locked.
******/
void wleUISelectionLockWarn(void)
{
	if (!editorUIState->lockMsgShown)
	{
		emStatusPrintf("The selection lock is enabled; disable it to change the selection.");
		editorUIState->lockMsgShown = true;
	}
}

/******
* This function centers the camera to make all of the specified editor objects viewable according to
* their type's bounding box function.
* PARAMS:
*   edObjs - EArray of EditorObjects to center in the viewport
******/
void wleUIFocusCameraOnEdObjs(EditorObject **edObjs)
{
	Vec3 aaMin, aaMax;
	Vec3 currMin, currMax;
	int i;
	bool found = false;

	setVec3(aaMin, 8e16, 8e16, 8e16);
	setVec3(aaMax, -8e16, -8e16, -8e16);
	for (i = 0; i < eaSize(&edObjs); i++)
	{
		editorObjectRef(edObjs[i]);
		if (edObjs[i]->type->getBoundsFunc)
		{
			found = true;
			edObjs[i]->type->getBoundsFunc(edObjs[i], currMin, currMax);
			vec3RunningMin(currMin, aaMin);
			vec3RunningMax(currMax, aaMax);
		}
		editorObjectDeref(edObjs[i]);
	}

	if (found)
	{
		GfxCameraController *camera = gfxGetActiveCameraController();
		Vec3 mid;
		float dist; 

		// center camera
		addVec3(aaMin, aaMax, mid);
		scaleVec3(mid, 0.5f, mid);

		// calculate distance needed for point to lie in frustum of camera
		// TODO: make this algorithm better
		dist = 2 * distance3(mid, aaMin);

		if (emIsFreecamActive())
		{
			Mat4 mat;
			Vec3 toCam;

			gfxGetActiveCameraMatrix(mat);
			scaleVec3(mat[2], dist, toCam);
			addVec3(mid, toCam, mid);
			gfxCameraControllerSetTarget(camera, mid);
		}
		else
		{
			gfxCameraControllerSetTarget(camera, mid);
			camera->camdist = dist;
		}
	}
}

/******
* This function pops up a dialog that warns the user that all changes will be saved (and journal will
* be cleared).
* PARAMS:
*   okFunc - UIDialogResponseCallback that will be called with the user's action in the popup dialog
*   okData - UserData passed to okFunc
******/
static void wleUISaveWarningConfirm(UIDialogResponseCallback okFunc, UserData okData)
{
	UIDialog *dialog = ui_DialogCreateEx("WARNING",
		"This operation will save the map's current contents. Do you want to continue?",
		okFunc, okData, NULL,
		"Cancel", kUIDialogButton_Cancel,
		"OK", kUIDialogButton_Ok,
		NULL);
	ui_WindowShow(UI_WINDOW(dialog));
	ui_SetFocus(UI_WIDGET(dialog)->children[2]);
}

static void wleUISetLayerModeOpenNode(ZoneMapLayer *layer)
{
	UITreeNode *trackerTreeRoot = &editorUIState->trackerTreeUI.trackerTree->root;
	int i;

	wleOpRefreshUI();
	if (!layer)
		return;
	for (i = 0; i < eaSize(&trackerTreeRoot->children); i++)
	{
		UITreeNode *node = trackerTreeRoot->children[i];
		EditorObject *edObj = node->contents;

		if (!edObj || edObj->type->objType != EDTYPE_LAYER)
			continue;
		if (edObj->obj == layer)
		{
			ui_TreeNodeExpand(node);
			break;
		}
	}
}

void wleUIClearEditorCopy(DisplayMessage* pDisplayMessage, void* ignored)
{
	if( pDisplayMessage && pDisplayMessage->pEditorCopy) {
		const char* messageKey = pDisplayMessage->pEditorCopy->pcMessageKey;
		
		StructDestroySafe( parse_Message, &pDisplayMessage->pEditorCopy );
		if( RefSystem_ReferentFromString( gMessageDict, messageKey )) {
			SET_HANDLE_FROM_STRING( gMessageDict, messageKey, pDisplayMessage->hMessage );
		}
	}
}

bool wleUISetLayerModeCallback(ServerResponseStatus status, ZoneMapLayer *layer, int step, int total_steps)
{
	if (status == StatusSuccess)
	{
		layer->locked = 3;
		layer->layer_mode = LAYER_MODE_EDITABLE;
		wleUISetLayerModeOpenNode(layer);

		FOR_EACH_IN_STASHTABLE(layer->grouptree.def_lib->defs, GroupDef, def) {
			langForEachDisplayMessage(parse_GroupDef, def, wleUIClearEditorCopy, NULL);
		} FOR_EACH_END;

		if (gLogResourceRequests)
			filelog_printf("resourceSend.log","Layer locked: %s", layer->filename);
	}
	return true;
}

bool wleUISetLayerModeUnlocked(ServerResponseStatus status, ZoneMapLayer *layer, int step, int total_steps)
{
	if (status == StatusInProgress)
	{
		progressOverlaySetValue(layer->progress_id, step);
		progressOverlaySetSize(layer->progress_id, total_steps);
		return false;
	}
	if (status == StatusSuccess)
	{
		if (layer->target_mode < layer->layer_mode)
		{
			wleUISetLayerMode(layer, layer->target_mode, true);
		}
		progressOverlayRelease(layer->progress_id);
	}
	return true;
}

bool wleUISetLayersModeUnlocked(ServerResponseStatus status, ZoneMapLayer **layers, int step, int total_steps)
{
	if (status == StatusInProgress)
	{
		return false;
	}
	if (status == StatusSuccess)
	{
		int i;
		for (i = 0; layers[i] != NULL; i++)
		{
			ZoneMapLayer *layer = layers[i];
			if (layer->target_mode < layer->layer_mode)
			{
				wleUISetLayerMode(layer, layer->target_mode, true);
			}
		}
	}
	SAFE_FREE(layers);
	return true;
}

/**********************
* FILE CLOSE CONFIRM
**********************/
static void wleUIFileCloseDialog(void);

static void wleUIContinueFileClose(void)
{
	if (eaSize(&editorUIState->closingLayers) > 0)
		wleUIFileCloseDialog();
	else
	{
		elUIWindowClose(NULL, editorUIState->closingFilesWin);
		editorUIState->closingFilesWin = NULL;
	}
}

static void wleUILayerUnlock(ZoneMapLayer *layer)
{
	char label[64];
	sprintf(label, "Binning %s...", layerGetName(layer));
	layer->progress_id = progressOverlayCreate(eaSize(&layer->terrain.blocks), label);
	wleOpUnlockFile(layerGetFilename(layer), wleUISetLayerModeUnlocked, layer);
}

static void wleUIFileSaveFromPrompt(UIButton *button, void *data)
{
	if (eaSize(&editorUIState->closingLayers) > 0)
	{
		ZoneMapLayer *layer = editorUIState->closingLayers[0];
		if (!layer->saving)
		{
			layerSave(layer, false, true);
			if (layer->saving)
			{
				layer->unlock_on_save = true;
				layer->terrain.source_data->source->unlock_on_save++;
			}
			else
			{
				wleUILayerUnlock(layer);
			}
		}
		eaRemove(&editorUIState->closingLayers, 0);
		objectLibrarySave(NULL);
	}
	wleUIContinueFileClose();
}

static void wleUIFileRevertFromPrompt(UIButton *button, void *data)
{
	if (eaSize(&editorUIState->closingLayers) > 0)
	{
		ZoneMapLayer *layer = editorUIState->closingLayers[0];
		layer->terrain.unsaved_changes = false;
		wleUILayerUnlock(layer);
		eaRemove(&editorUIState->closingLayers, 0);
	}
	wleUIContinueFileClose();
}

static void wleUIFileSaveAllFromPrompt(UIButton *button, void *data)
{
	int i;
	for (i = 0; i < eaSize(&editorUIState->closingLayers); i++)
	{
		ZoneMapLayer *layer = editorUIState->closingLayers[i];
		if (!layer->saving)
		{
			layerSave(layer, false, true);
			if (layer->saving)
			{
				layer->unlock_on_save = true;
				layer->terrain.source_data->source->unlock_on_save++;
			}
			else
				wleUILayerUnlock(layer);
		}
	}
	objectLibrarySave(NULL);
	eaClear(&editorUIState->closingLayers);
	wleUIContinueFileClose();
}

static void wleUIFileRevertAllFromPrompt(UIButton *button, void *data)
{
	int i;
	const char **filenames = NULL;
	ZoneMapLayer **layers = calloc(1, (eaSize(&editorUIState->closingLayers)+1)*sizeof(ZoneMapLayer *));
	for (i = 0; i < eaSize(&editorUIState->closingLayers); i++)
	{
		ZoneMapLayer *layer = editorUIState->closingLayers[i];
		layer->terrain.unsaved_changes = false;
		eaPush(&filenames, layerGetFilename(layer));
		layers[i] = layer;
	}
	wleOpUnlockFiles(filenames, false, wleUISetLayersModeUnlocked, layers);
	eaClear(&editorUIState->closingLayers);
	wleUIContinueFileClose();
}

static void wleUIFileCancelFromPrompt(UIButton *button, void *data)
{
	int i;
	for (i = 0; i < eaSize(&editorUIState->closingLayers); i++)
		editorUIState->closingLayers[i]->target_mode = editorUIState->closingLayers[i]->layer_mode;
	eaClear(&editorUIState->closingLayers);
	wleUIContinueFileClose();
}

static void wleUIFileCloseDialog(void)
{
	UIWindow *window;
	UILabel *label;
	UIButton *button;
	char buf[1024];
	F32 y = 0;
	const char *name;

	if (eaSize(&editorUIState->closingLayers) == 0)
		return;

	if (editorUIState->closingFilesWin)
		elUIWindowClose(NULL, editorUIState->closingFilesWin);

	// Create the prompt window
	window = ui_WindowCreate("Save Before Close?", 0, 0, 400, 50);
	ui_WindowSetModal(window, true);
	ui_WindowSetClosable(window, false);

	// Lay out the message
	name = layerGetFilename(editorUIState->closingLayers[0]);

	sprintf(buf,"Layer \"%s\" is not saved.  Do you want to save it?", name);
	label = ui_LabelCreate(buf,50,0);
	ui_WidgetSetPositionEx(UI_WIDGET(label),-label->widget.width/2, 0, 0.5, 0, UITopLeft);
	ui_WindowAddChild(window, label);

	// Ensure the window is big enough
	window->widget.width = MIN(MAX(100 + label->widget.width,530),1000);

	if (eaSize(&editorUIState->closingLayers) > 1)
		y = 28;

	// Create the buttons
	button = ui_ButtonCreate("Save",0,0,wleUIFileSaveFromPrompt,NULL);
	button->widget.width = 120;
	ui_WidgetSetPositionEx(UI_WIDGET(button),-190, y, 0.5, 0, UIBottomLeft);
	ui_WindowAddChild(window, button);

	button = ui_ButtonCreate("Discard Changes",0,0,wleUIFileRevertFromPrompt,NULL);
	button->widget.width = 120;
	ui_WidgetSetPositionEx(UI_WIDGET(button),-60, y, 0.5, 0, UIBottomLeft);
	ui_WindowAddChild(window, button);

	button = ui_ButtonCreate("Cancel Close",0,0,wleUIFileCancelFromPrompt,NULL);
	button->widget.width = 120;
	ui_WidgetSetPositionEx(UI_WIDGET(button),70, y, 0.5, 0, UIBottomLeft);
	ui_WindowAddChild(window, button);

	if (eaSize(&editorUIState->closingLayers) > 1)
	{
		// Create the buttons
		button = ui_ButtonCreate("Save All",0,0,wleUIFileSaveAllFromPrompt,NULL);
		button->widget.width = 120;
		ui_WidgetSetPositionEx(UI_WIDGET(button),-190, 0, 0.5, 0, UIBottomLeft);
		ui_WindowAddChild(window, button);

		button = ui_ButtonCreate("Discard All",0,0,wleUIFileRevertAllFromPrompt,NULL);
		button->widget.width = 120;
		ui_WidgetSetPositionEx(UI_WIDGET(button),-60, 0, 0.5, 0, UIBottomLeft);
		ui_WindowAddChild(window, button);

		window->widget.height += 28;
	}

	// Store the prompt state
	editorUIState->closingFilesWin = window;

	// Show the window
	elUICenterWindow(window);
	ui_WindowPresent(window);
}

void wleUILayerCloseDialog(ZoneMapLayer *layer)
{
	int i;
	bool found = false;

	for (i = 0; i < eaSize(&editorUIState->closingLayers); i++)
	{
		if (editorUIState->closingLayers[i] == layer)
		{
			found = true;
			break;
		}
	}

	if (!found)
		eaPush(&editorUIState->closingLayers, layer);

	wleUIFileCloseDialog();
}


/********************
* LAYER MODE CHANGE
********************/
/******
* This function sets the active layer mode.
* PARAMS:
*   layer - ZoneMapLayer to lock
******/
void wleUISetLayerModeEx(ZoneMapLayer *layer, ZoneMapLayerMode mode, bool closing, bool force)
{
	if (layer->layer_mode == mode)
		return;

	layer->target_mode = mode;
	if (layerGetMode(layer) == LAYER_MODE_EDITABLE && mode < LAYER_MODE_EDITABLE && !closing)
	{
		const char *layerFilename = layerGetFilename(layer);

		if (!layerFilename)
		{
			layer->target_mode = layer->layer_mode;
			return;
		}

		if (!force && layerGetUnsaved(layer))
		{
			wleUILayerCloseDialog(layer);
			return;
		}

		layer->layer_mode = LAYER_MODE_TERRAIN; // Prevent further editing
		wleUILayerUnlock(layer);
		return;
	}
	if (mode == LAYER_MODE_EDITABLE)
	{
        // Attempt to checkout all the files we need.
        if (!terrainCheckoutLayer(layer, NULL, 0, false))
        {
			layer->target_mode = layer->layer_mode;
            return;
        }
//		layerUnload(layer);
	}

	// Can't set mode directly to EDITABLE here
	layerSetMode(layer, MIN(LAYER_MODE_TERRAIN, mode), true, false, true);
	wleUISetLayerModeOpenNode(layer);

	if (mode == LAYER_MODE_EDITABLE)
	{
		if (layer->locked == 3) {
			layer->layer_mode = LAYER_MODE_EDITABLE;
		} else if (layer->scratch) {
			// Scratch layer does not have to be locked
			wleUISetLayerModeCallback(StatusSuccess, layer, 0, 1);
		} else {
			wleOpLockFile(layerGetFilename(layer), wleUISetLayerModeCallback, layer);
		}
	}
}

/******
* This function wraps the locking functionality for layers.
* PARAMS:
*   layer - ZoneMapLayer to lock
******/
void wleUILockLayerWrapper(ZoneMapLayer *layer)
{
	wleUISetLayerMode(layer, LAYER_MODE_EDITABLE, false);
}

/******
* This function wraps the unlocking functionality for layers, possibly invoking the save and unlock
* operation.
* PARAMS:
*   layer - ZoneMapLayer to unlock
******/
void wleUIUnlockLayerWrapper(ZoneMapLayer *layer)
{
	wleUISetLayerMode(layer, LAYER_MODE_TERRAIN, false);
}

/******
* This function wraps the locking functionality for group files.
* PARAMS:
*   layer - ZoneMapLayer to lock
******/
void wleUILockGroupFileWrapper(GroupDefLockedFile *gfile)
{
	objectLibrarySetFileEditable(objectLibraryGetFilePath(gfile));
}

static void wleUILayerMenuViewGroupTree(UIMenuItem *item, ZoneMapLayer *layer)
{
	wleUISetLayerMode(layer, LAYER_MODE_GROUPTREE, false);
}

static void wleUILayerMenuLock(UIMenuItem *item, ZoneMapLayer *layer)
{
	if (layerGetLocked(layer) == 3)
		wleUIUnlockLayerWrapper(layer);
	else if (layerGetLocked(layer) != 2)
		wleUILockLayerWrapper(layer);
}

static void wleUILayerMenuOpenFolder(UIMenuItem *item, ZoneMapLayer *layer)
{
	emuOpenContainingDirectory(layerGetFilename(layer));
}

/******
* This function takes a menu and a layer, and it appends relevant menu items to the menu regarding layer
* operations.
* PARAMS:
*   menu - UIMenu to which layer items will be appended
*   layer - ZoneMapLayer whose items will be appended
******/
static void wleUILayerMenuItemsAdd(UIMenu *menu, ZoneMapLayer *layer)
{
	char itemText[MAX_PATH + 10];
	char *c;

	// add viewing menu item
	if (layerGetMode(layer) != LAYER_MODE_EDITABLE)
		ui_MenuAppendItem(menu, ui_MenuItemCreate("View Geometry", UIMenuCallback, wleUILayerMenuViewGroupTree, layer, NULL));

	// add locking menu item
	if (layerGetLocked(layer) != 2)
		ui_MenuAppendItem(menu, ui_MenuItemCreate(layerGetLocked(layer) == 3 ? "Release" : "Edit", UIMenuCallback, wleUILayerMenuLock, layer, NULL));

	sprintf(itemText, "Open \"%s", layerGetFilename(layer));
	c = strrchr(itemText, '/');
	if (c)
		*(c + 1) = '\0';
	strcat(itemText, "\"");
	ui_MenuAppendItem(menu, ui_MenuItemCreate(itemText, UIMenuCallback, wleUILayerMenuOpenFolder, layer, NULL));
}

static void wleUIGroupFileMenuOpenFolder(UIMenuItem *item, GroupDefLockedFile *file)
{
	emuOpenContainingDirectory(objectLibraryGetFilePath(file));
}

/******
* This function takes a menu and a group file, and it appends relevant menu items to the menu regarding
* group file operations.
* PARAMS:
*   menu - UIMenu to which layer items will be appended
*   layer - ZoneMapLayer whose items will be appended
******/
static void wleUIGroupFileMenuItemsAdd(UIMenu *menu, GroupDefLockedFile *file)
{
	char itemText[MAX_PATH + 10];
	char *c;

	sprintf(itemText, "Open \"%s", objectLibraryGetFilePath(file));
	c = strrchr(itemText, '/');
	if (c)
		*(c + 1) = '\0';
	strcat(itemText, "\"");
	ui_MenuAppendItem(menu, ui_MenuItemCreate(itemText, UIMenuCallback, wleUIGroupFileMenuOpenFolder, file, NULL));
}


/********************
* MENU CALLBACKS
********************/
bool wleUIVolumeTypeHideCheck(UserData unused)
{
	GfxVisualizationSettings *visSettings = gfxGetVisSettings();
	return !visSettings->hide_all_volumes;
}

/********************
* TOOLBAR CALLBACKS
********************/
static void wleUIFiltersEditClicked(UIButton *button, UIComboBox *combo)
{
	WleFilter *filter = ui_ComboBoxGetSelectedObject(combo);
	wleOptionsFilterEdit(filter);
}

static void wleUIFilterSelected(UIComboBox *combo, UserData unused)
{
	char *prefName;
	WleFilter *filter = ui_ComboBoxGetSelectedObject(combo);

	if (combo == editorUIState->trackerSearchUI.searchFilterCombo)
		prefName = "SearchFilter";
	else if (combo == editorUIState->toolbarUI.marqueeFilterCombo)
		prefName = "MarqueeFilter";
	else
		return;

	EditorPrefStoreString(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, prefName, filter ? filter->name : "");
}

void wleToolbarEditOrigRefresh(void)
{
	ui_ButtonSetImage(editorUIState->toolbarUI.editOrigButton, (editState.editOriginal ? "eui_icon_editinstance_ON" : "eui_icon_editinstance_OFF"));
	ui_WidgetSetTooltipString(UI_WIDGET(editorUIState->toolbarUI.editOrigButton), 
		(editState.editOriginal ? "Edit Instances is ON" : "Edit Instances is OFF"));
}

static void wleToolbarEditOrigClicked(UIButton *button, UserData unused)
{
	wleCmdEditOrig();
}

void wleToolbarSelectionLockRefresh(void)
{
	ui_ButtonSetImage(editorUIState->toolbarUI.lockButton, (editState.lockedSelection ? "eui_icon_lockselect_ON" : "eui_icon_lockselect_OFF"));
	ui_WidgetSetTooltipString(UI_WIDGET(editorUIState->toolbarUI.lockButton), 
		(editState.lockedSelection ? "Lock Selection is ON" : "Lock Selection is OFF"));
}

static void wleToolbarSelectionLockClicked(UIButton *button, UserData unused)
{
	wleCmdLockSelection();
}

void wleToolbarMarqueeCrossingRefresh(void)
{
	ui_ButtonSetImage(editorUIState->toolbarUI.marqueeCrossingButton, (edObjHarnessGetMarqueeCrossingMode() ? "eui_icon_marquee_crossing_ON" : "eui_icon_marquee_notcrossing_ON"));
	ui_WidgetSetTooltipString(UI_WIDGET(editorUIState->toolbarUI.marqueeCrossingButton), 
		(edObjHarnessGetMarqueeCrossingMode() ? "Marquee Crossing Mode is ON" : "Marquee Crossing Mode is OFF"));
}

static void wleToolbarMarqueeCrossingClicked(UIButton *button, UserData unused)
{
	edObjHarnessSetMarqueeCrossingMode(!edObjHarnessGetMarqueeCrossingMode());
	wleToolbarMarqueeCrossingRefresh();
}

static void wleToolbarMarqueeFilterClear(UIButton *button, UserData unused)
{
	ui_ComboBoxSetSelectedObjectAndCallback(editorUIState->toolbarUI.marqueeFilterCombo, NULL);
}

static void toggleSplineDrawMode(UIButton *button, UserData unused)
{
	editState.splineDrawOnTerrain = !editState.splineDrawOnTerrain;

	ui_ButtonSetText(button, 
		(editState.splineDrawOnTerrain ? "Draw on TERRAIN" : "Draw on GRID"));
	ui_WidgetSetTooltipString((UIWidget*)button, 
		(editState.splineDrawOnTerrain ? "Draw on TERRAIN" : "Draw on GRID"));
}

static void setSplineDrawHeight(UITextEntry *entry, UserData unused)
{
	F32 height;
	if (sscanf(ui_TextEntryGetText(entry), "%f", &height) && height != editState.splineDrawOffset)
	{
		editState.splineDrawOffset = height;
	}
	else
	{
		char buf[64];
		sprintf(buf, "%0.2f", editState.splineDrawOffset);
		ui_TextEntrySetText(entry, buf);
	}
}

static void setTeamSize(UISliderTextEntry *slidertext, bool bFinished, UserData unused)
{
	gEditorTeamSize = (int)ui_SliderTextEntryGetValue(slidertext);
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "EncounterTeamSize", gEditorTeamSize);
	zmapTrackerUpdate(NULL, true, false);
	wleOpRefreshUI();
}

void wleUIGizmoModeEnable(bool active)
{
	ui_SetActive(UI_WIDGET(editorUIState->toolbarUI.gizmoModeCombo), active);
}

void wleUIGizmoModeRefresh(void)
{
	ui_ComboBoxSetSelected(editorUIState->toolbarUI.gizmoModeCombo, editState.gizmoMode);
}

static void wleUIToolbarGizmoModeSelected(UIComboBox *cb, UserData unused)
{
	wleSetGizmoMode(ui_ComboBoxGetSelected(cb));
}

ZoneMapLayer* wleGetScratchLayer(bool make)
{
	ZoneMapLayer *layer;
	const char *local_data_dir = fileLocalDataDir();
	char scratch_layer_name[MAX_PATH];
	sprintf(scratch_layer_name, "%s/%s", local_data_dir, "editor/World_Editor_Scratch_Layer/World_Editor_Scratch_Layer.layer"); 
	layer = zmapGetLayerByName(NULL, scratch_layer_name);
	if(!layer && make) {
		ZoneMap *active_map = worldGetActiveMap();
		layer = zmapNewLayer(active_map, zmapGetLayerCount(NULL), scratch_layer_name);
		assert(layer);
		layerSetUnsaved(layer, false);
		layer->scratch = true;
		wleUISetLayerMode(layer, LAYER_MODE_EDITABLE, false);
	}
	return layer;
}

void wleToolbarUpdateEditingScratchLayer(ZoneMapLayer *layer)
{
	if(editorUIState) {
		if(!layer)
			layer = wleGetScratchLayer(false);
		if(!layer)
			editorUIState->showingScratchLayer = false;
		ui_ButtonSetText(editorUIState->toolbarUI.scratchLayerToggleButton, (editorUIState->showingScratchLayer ? "Hide Scratch" : "Show Scratch"));
	}
}

void wleToolbarToggleEditingScratchLayer()
{
	if(editorUIState) {
		ZoneMapLayer *layer = wleGetScratchLayer(true);
		TrackerHandle *handle = trackerHandleCreate(layerGetTracker(layer));

		editorUIState->showingScratchLayer = !editorUIState->showingScratchLayer;

		if(handle) {
			if(editorUIState->showingScratchLayer) {
				wleTrackerUnhide(handle);
			} else {
				wleTrackerHide(handle);
			}
			trackerHandleDestroy(handle);
		}

		wleToolbarUpdateEditingScratchLayer(layer);
		wleOpRefreshUI();
	}
}

static void wleUIToolbarToggleEditingScratchLayerCB(UIButton *button, UserData unused)
{
	wleToolbarToggleEditingScratchLayer();
}

static UITreeRefreshNode **wleUITrackerTreeGetRefreshNode(UITreeRefreshNode *refreshRoot, UITreeNode *root, UITreeNode *node, UITreeRefreshNode **parent)
{
	int i, j;

	for (i = 0; i < eaSize(&root->children); i++)
	{
		UITreeNode *childNode = root->children[i];
		for (j = 0; j < eaSize(&refreshRoot->children); j++)
		{
			UITreeRefreshNode *childRefreshNode = refreshRoot->children[j];
			if (childNode->crc == childRefreshNode->nodeCrc)
			{
				if (childNode == node)
				{
					*parent = refreshRoot;
					return &refreshRoot->children[j];
				}
				else
				{
					UITreeRefreshNode **ret = wleUITrackerTreeGetRefreshNode(childRefreshNode, childNode, node, parent);
					if (ret)
						return ret;
				}
			}
		}
	}

	*parent = NULL;
	return NULL;
}

static void wleUITrackerTreeRefreshForEdObj(EditorObject *edObj)
{
	if (!edObj)
	{
		editorUIState->trackerTreeUI.currRefreshNode = &editorUIState->trackerTreeUI.topRefreshNode;
		wleUITrackerTreeRefresh(editorUIState->trackerTreeUI.topRefreshNode);
	}
	else
	{
		TrackerHandle *temp = editorUIState->trackerTreeUI.activeScopeTracker;
		UITreeRefreshNode *parentNode = NULL;
		UITreeNode *edObjNode;

		editorObjectRef(edObj);

		// rebuild entire tree normally
		editorUIState->trackerTreeUI.activeScopeTracker = NULL;
		ui_TreeRefreshEx(editorUIState->trackerTreeUI.trackerTree, editorUIState->trackerTreeUI.topRefreshNode);
		editorUIState->trackerTreeUI.activeScopeTracker = temp;

		// refresh tree at relevant area
		edObjNode = wleUITrackerTreeGetNodeForEdObj(edObj);
		if (edObjNode)
			editorUIState->trackerTreeUI.currRefreshNode = wleUITrackerTreeGetRefreshNode(editorUIState->trackerTreeUI.topRefreshNode, &editorUIState->trackerTreeUI.trackerTree->root, edObjNode, &parentNode);
		wleUITrackerTreeRefresh(parentNode);

		editorObjectDeref(edObj);
	}
}

void wleToolbarGroupTree()
{
	if(editorUIState && editorUIState->showingLogicalTree)
	{
		GroupTracker *scopeTracker = NULL;

		if (editorUIState->trackerTreeUI.logicalTreeRefreshNode)
			ui_TreeRefreshNodeDestroy(editorUIState->trackerTreeUI.logicalTreeRefreshNode);
		editorUIState->trackerTreeUI.logicalTreeRefreshNode = ui_TreeRefreshNodeCreate(&editorUIState->trackerTreeUI.trackerTree->root);

		editorUIState->showingLogicalTree = false;
		editorUIState->showingLogicalNames = false;

		if (editorUIState->trackerTreeUI.activeScopeTracker)
		{
			trackerHandleDestroy(editorUIState->trackerTreeUI.activeScopeTracker);
			editorUIState->trackerTreeUI.activeScopeTracker = NULL;
			editorUIState->trackerTreeUI.currRefreshNode = &editorUIState->trackerTreeUI.topRefreshNode;
		}

		scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);

		// POTENTIAL CRASH: in here when we try to refresh the tracker tree when there is a refresh node tree nested inside the topRefreshNode tree?
		// FIXED ACTUAL CRASH [COR-15474]: in UITree there was a refresh node optimization that tried to add the zero CRC to a stash table. Zero is an invalid stash table key.
		// However, the tracker refresh system may need a redesign to avoid these problems.
		wleUITrackerTreeRefreshForEdObj(scopeTracker && scopeTracker->def ? editorObjectCreate(trackerHandleCreate(scopeTracker), scopeTracker->def->name_str, scopeTracker->parent_layer, EDTYPE_TRACKER) : NULL);

		wleOpRefreshUI();
	}
}

static void wleUIToolbarGroupTreeCB(UIButton *button, UserData unused)
{
	wleToolbarGroupTree();
}

void wleToolbarLogicalTree()
{
	if(editorUIState && !editorUIState->showingLogicalTree)
	{
		// POTENTIAL BUG REPRO: Create a Scoped object, click Scope button in tracker, Undo, change to Showing GroupTree, potential crash?

		if (editorUIState->trackerTreeUI.activeScopeTracker)
		{
			trackerHandleDestroy(editorUIState->trackerTreeUI.activeScopeTracker);
			editorUIState->trackerTreeUI.activeScopeTracker = NULL;
		}

		// save the current state of the tree into the stored refresh node
		if (*editorUIState->trackerTreeUI.currRefreshNode)
			ui_TreeRefreshNodeDestroy(*editorUIState->trackerTreeUI.currRefreshNode);	// POTENTIAL BUG: destroying a refresh node that is inside the
																						// topRefreshNode tree!

		*editorUIState->trackerTreeUI.currRefreshNode = ui_TreeRefreshNodeCreate(&editorUIState->trackerTreeUI.trackerTree->root);	// POTENTIAL BUG cont: down here, we are
																																	// creating another refresh node tree at the
																																	// position inside the topRefreshNode tree
																																	// where the currRefreshNode pointed!

		editorUIState->showingLogicalTree = true;
		editorUIState->showingLogicalNames = true;

		wleUITrackerTreeRefresh(editorUIState->trackerTreeUI.logicalTreeRefreshNode);

		wleOpRefreshUI();
	}
}

static void wleUIToolbarLogicalTreeCB(UIButton *button, UserData unused)
{
	wleToolbarLogicalTree();
}

void wleToolbarGroupNames()
{
	if(editorUIState && editorUIState->showingLogicalNames)
	{
		editorUIState->showingLogicalNames = false;

		wleOpRefreshUI();
	}
}

static void wleUIToolbarGroupNamesCB(UIButton *button, UserData unused)
{
	wleToolbarGroupNames();
}

void wleToolbarLogicalNames()
{
	if(editorUIState && !editorUIState->showingLogicalNames)
	{
		editorUIState->showingLogicalNames = true;

		wleOpRefreshUI();
	}
}

static void wleUIToolbarLogicalNamesCB(UIButton *button, UserData unused)
{
	wleToolbarLogicalNames();
}

/******
* This function creates the world editor's toolbars.
******/
void wleUIToolbarInit(void)
{
	EMToolbar *toolbar = emToolbarCreateEx("Selection", 0);
	int buttonWidth = emToolbarGetHeight(toolbar);
	UIButton *button;
	UIComboBox *combo;
	UISliderTextEntry *slidertext;
	void ***snapTypes = calloc(1, sizeof(void **));
	void ***gizmoModes = calloc(1, sizeof(void **));
	UIScrollArea *miniBar;
	UILabel *label;
	UITextEntry *entry;
	bool state;

	PERFINFO_AUTO_START_FUNC();

	if (eaSize(&worldEditor.toolbars) > 0)
		return;
	eaPush(&worldEditor.toolbars, emToolbarCreateWindowToolbar());
	eaPush(&worldEditor.toolbars, toolbar);

	// edit orig
	button = ui_ButtonCreateImageOnly("eui_icon_editinstance_OFF", 0, 0, wleToolbarEditOrigClicked, NULL);
	ui_WidgetSetDimensions((UIWidget*) button, buttonWidth, buttonWidth);
	ui_ButtonSetImageStretch(button, true);
	emToolbarAddChild(toolbar, button, false);
	editorUIState->toolbarUI.editOrigButton = button;
	editState.editOriginal = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "EditOriginal", 0);
	wleToolbarEditOrigRefresh();

	// selection lock
	button = ui_ButtonCreateImageOnly("eui_icon_lockselect_OFF", elUINextX(button) + 5, 0, wleToolbarSelectionLockClicked, NULL);
	ui_WidgetSetDimensions((UIWidget*) button, buttonWidth, buttonWidth);
	ui_ButtonSetImageStretch(button, true);
	emToolbarAddChild(toolbar, button, false);
	editorUIState->toolbarUI.lockButton = button;
	wleToolbarSelectionLockRefresh();

	// marquee selection mode
	button = ui_ButtonCreateImageOnly("eui_icon_marquee_notcrossing_ON", elUINextX(button), 0, wleToolbarMarqueeCrossingClicked, NULL);
	ui_WidgetSetDimensions((UIWidget*) button, buttonWidth, buttonWidth);
	ui_ButtonSetImageStretch(button, true);
	emToolbarAddChild(toolbar, button, false);
	editorUIState->toolbarUI.marqueeCrossingButton = button;
	wleToolbarMarqueeCrossingRefresh();

	// pivot mode combo box
	eaPush(gizmoModes, "Object");
	eaPush(gizmoModes, "Pivot");
	eaPush(gizmoModes, "Temporary Pivot");
	label = ui_LabelCreate("Pivot mode", elUINextX(button) + 5, 0);
	emToolbarAddChild(toolbar, label, false);
	combo = ui_ComboBoxCreate(elUINextX(label) + 5, 0, 120, NULL, gizmoModes, NULL);
	editorUIState->toolbarUI.gizmoModeCombo = combo;
	combo->widget.height = buttonWidth;
	ui_ComboBoxSetSelectedCallback(combo, wleUIToolbarGizmoModeSelected, &combo->iSelected);
	ui_ComboBoxSetSelected(combo, editState.gizmoMode);
	emToolbarAddChild(toolbar, combo, true);

	// selection filter
	toolbar = emToolbarCreateEx("SelectionFilter", 0);
	eaPush(&worldEditor.toolbars, toolbar);
	label = ui_LabelCreate("Selection filter", 0, 0);
	emToolbarAddChild(toolbar, label, false);
	combo = ui_ComboBoxCreate(elUINextX(label) + 5, 0, 150, parse_WleFilter, &editorUIState->searchFilters->filters, "name");
	editorUIState->toolbarUI.marqueeFilterCombo = combo;
	ui_ComboBoxSetSelectedCallback(combo, wleUIFilterSelected, NULL);
	emToolbarAddChild(toolbar, combo, false);
	button = ui_ButtonCreate("Clear", elUINextX(combo), 0, wleToolbarMarqueeFilterClear, NULL);
	emToolbarAddChild(toolbar, button, false);
	button = ui_ButtonCreate("Edit Filters", elUINextX(button), 0, wleUIFiltersEditClicked, combo);
	emToolbarAddChild(toolbar, button, true);

	// gizmos toolbar
	toolbar = emToolbarCreateEx("Gizmos", 0);
	eaPush(&worldEditor.toolbars, toolbar);
	editorUIState->gizmosToolbar = elGizmosToolbarCreate(wleGizmosUIGizmoChanged, buttonWidth);
	elGizmosToolbarAddTranslateGizmo(editorUIState->gizmosToolbar, edObjHarnessGetTransGizmo(), "Translate", wleGizmosUITransGizmoChanged);
	elGizmosToolbarAddRotateGizmo(editorUIState->gizmosToolbar, edObjHarnessGetRotGizmo(), "Rotate", wleGizmosUIRotGizmoChanged);
	miniBar = elGizmosToolbarGetWidget(editorUIState->gizmosToolbar);
	emToolbarAddChild(toolbar, miniBar, true);

	// curve toolbar
	toolbar = emToolbarCreateEx("Curves", 0);
	state = EditorPrefGetInt(worldEditor.editor_name, TOOLBAR_VIS_PREF, "Curves", 0);
	EditorPrefStoreInt(worldEditor.editor_name, TOOLBAR_VIS_PREF, "Curves", state);
	eaPush(&worldEditor.toolbars, toolbar);
	button = ui_ButtonCreate("Draw on GRID", 0, 0, toggleSplineDrawMode, NULL);
	emToolbarAddChild(toolbar, button, false);
	label = ui_LabelCreate("Height", elUINextX(button) + 5, 0);
	emToolbarAddChild(toolbar, label, false);
	entry = ui_TextEntryCreate("0", elUINextX(label) + 5, 0);
	entry->widget.width = 50;
	ui_TextEntrySetEnterCallback(entry, setSplineDrawHeight, NULL);
	emToolbarAddChild(toolbar, entry, true);

	// Encounter toolbar
	toolbar = emToolbarCreateEx("Encounters", 0);
	eaPush(&worldEditor.toolbars, toolbar);
	label = ui_LabelCreate("Team Size", 0, 0);
	emToolbarAddChild(toolbar, label, false);
	slidertext = ui_SliderTextEntryCreate("1", 1, 5, elUINextX(label) + 5, 0, 140);
	ui_SliderTextEntrySetRange(slidertext, 1, 5, 1);
	ui_SliderTextEntrySetPolicy(slidertext, UISliderContinuous);
	gEditorTeamSize = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "EncounterTeamSize", 1);
	ui_SliderTextEntrySetValue(slidertext, gEditorTeamSize);
	ui_SliderTextEntrySetChangedCallback(slidertext, setTeamSize, NULL);
	emToolbarAddChild(toolbar, slidertext, true);

	// initialize toolbar preferences
	TranslateGizmoSetAlignedToWorld(edObjHarnessGetTransGizmo(), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosWorldAligned", 0));
	TranslateGizmoSetSpecSnap(edObjHarnessGetTransGizmo(), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnap", EditSnapGrid));
	TranslateGizmoSetSnapResolution(edObjHarnessGetTransGizmo(), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnapRes", 2));
	TranslateGizmoSetSnapNormal(edObjHarnessGetTransGizmo(), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnapNormal", 0));
	TranslateGizmoSetSnapNormalAxis(edObjHarnessGetTransGizmo(), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnapNormalAxis", 1));
	TranslateGizmoSetSnapNormalInverse(edObjHarnessGetTransGizmo(), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnapNormalInv", 0));
	RotateGizmoEnableSnap(edObjHarnessGetRotGizmo(), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosRotSnap", 1));
	RotateGizmoSetSnapResolution(edObjHarnessGetRotGizmo(), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosRotSnapRes", 4));
	wleGizmosUITransGizmoChanged(edObjHarnessGetTransGizmo());
	wleGizmosUIRotGizmoChanged(edObjHarnessGetRotGizmo());

	PERFINFO_AUTO_STOP();
}


/********************
* TRACKER TREE
********************/
typedef struct WleUIEdObjNodeAssoc
{
	EditorObject *edObj;
	UITreeNode *node;
} WleUIEdObjNodeAssoc;

StashTable wleUIEdObjNodeAssocs;

static void wleUITrackerNodeFill(UITreeNode *parent, EditorObject *object);

/******
* This function stores the association between an EditorObject and its UITreeNode in
* the tracker tree.
* PARAMS:
*   edObj - EditorObject to associate
*   node - UITreeNode to associate
******/
static void wleUITrackerTreeAddEdObjNodeAssoc(EditorObject *edObj, UITreeNode *node)
{
	WleUIEdObjNodeAssoc ***assocs = NULL;
	WleUIEdObjNodeAssoc *newAssoc = calloc(1, sizeof(*newAssoc));

	if (!wleUIEdObjNodeAssocs)
		wleUIEdObjNodeAssocs = stashTableCreateWithStringKeys(32, StashDeepCopyKeys);
	if (!stashFindPointer(wleUIEdObjNodeAssocs, edObj->name, (void*) &assocs))
	{
		assocs = calloc(1, sizeof(*assocs));
		stashAddPointer(wleUIEdObjNodeAssocs, edObj->name, assocs, false);
	}
	newAssoc->edObj = edObj;
	newAssoc->node = node;
	eaPush(assocs, newAssoc);
}

/******
* This function gets the UITreeNode in the tracker tree tied to the specified EditorObject.
* PARAMS:
*   edObj - EditorObject whose node should be retrieved
* RETURNS:
*   UITreeNode associated with the specified EditorObject
******/
UITreeNode *wleUITrackerTreeGetNodeForEdObj(EditorObject *edObj)
{
	WleUIEdObjNodeAssoc ***assocs;

	if (!edObj)
		return NULL;

	if (stashFindPointer(wleUIEdObjNodeAssocs, edObj->name, (void*) &assocs))
	{
		int i;
		for (i = 0; i < eaSize(assocs); i++)
		{
			if (edObjCompareForUI(edObj, (*assocs)[i]->edObj) == 0)
				return (*assocs)[i]->node;
		}
	}

	return NULL;
}

/******
* This function gets the current EditorObject highlighted in the tracker tree.
* RETURNS:
*   EditorObject highlighted in the tracker tree
******/
EditorObject *wleUITrackerTreeGetSelectedEdObj(void)
{
	UITreeNode *selected = ui_TreeGetSelected(editorUIState->trackerTreeUI.trackerTree);
	if (selected)
		return selected->contents;
	else
		return NULL;
}

/******
* This function removes the association between an EditorObject and its node in the tracker
* tree.
* PARAMS:
*   edObj - EditorObject whose association should be removed
******/
static void wleUITrackerTreeRemoveEdObjNodeAssoc(EditorObject *edObj)
{
	WleUIEdObjNodeAssoc ***assocs;

	if (stashFindPointer(wleUIEdObjNodeAssocs, edObj->name, (void*) &assocs))
	{
		int i;
		for (i = 0; i < eaSize(assocs); i++)
		{
			if (edObjCompareForUI(edObj, (*assocs)[i]->edObj) == 0)
			{
				free(eaRemove(assocs, i));
				break;
			}
		}
		if (eaSize(assocs) == 0)
		{
			stashRemovePointer(wleUIEdObjNodeAssocs, edObj->name, (void*) &assocs);
			free(assocs);
		}
	}
}

/******
* This function calculates the width of any of the tracker tree, returning the width so it
* can be set on the tree.
* PARAMS:
*   node - UITreeNode from which to start calculating the width; the function recurses through all
*          of node's children
*   offset - F32 amount to add to the returned width; represents the left-identation padding
* RETURNS:
*   F32 maximum width of the text for the specified node and its children
******/
static F32 wleUITrackerTreeGetWidthMain(UITreeNode *node, F32 offset)
{
	AtlasTex *opened = (g_ui_Tex.minus);
	char message[1024] = {0};
	F32 maxWidth;
	int i;

	maxWidth = offset + (int)(intptr_t) node->displayData;
	for (i = 0; i < eaSize(&node->children); i++)
	{
		F32 childWidth = wleUITrackerTreeGetWidthMain(node->children[i], offset + UI_STEP + opened->width);
		maxWidth = MAX(maxWidth, childWidth);
	}

	return maxWidth;
}

/******
* This is a wrapper for wleUITrackerTreeGetWidthMain that sets the tracker tree's max width
* in order to render the horizontal scrollbar.
******/
static void wleUITrackerTreeGetWidth(UserData unused)
{
	editorUIState->trackerTreeUI.trackerTree->width = wleUITrackerTreeGetWidthMain(&editorUIState->trackerTreeUI.trackerTree->root, 10);
}


/******
* This is the global refresh function for the tracker tree.
******/
void wleUITrackerTreeRefresh(UITreeRefreshNode *refreshNode)
{
	PERFINFO_AUTO_START_FUNC();
	if (editorUIState && editorUIState->trackerTreeUI.trackerTree)
	{
		EditorObject *selected = wleUITrackerTreeGetSelectedEdObj();
		if (selected)
			editorObjectRef(selected);
		if (refreshNode)
			ui_TreeRefreshEx(editorUIState->trackerTreeUI.trackerTree, refreshNode);
		else
			ui_TreeRefresh(editorUIState->trackerTreeUI.trackerTree);
		if (selected)
		{
			wleUITrackerTreeHighlightEdObj(selected);
			editorObjectDeref(selected);
		}
		wleUITrackerTreeGetWidth(NULL);
	}

	wleUITrackerTreeDisplayModeRefresh();

	PERFINFO_AUTO_STOP();
}

static bool wleUIEdObjIsChildSelected(EditorObject *edObj)
{
	EditorObject **children = NULL;
	bool ret = false;
	if (edObj->type->childFunc)
	{
		int i;

		edObj->type->childFunc(edObj, &children);
		for (i = 0; i < eaSize(&children); i++)
		{
			editorObjectRef(children[i]);
			if (edObjFindSelected(children[i]))
				ret = true;
			else
				ret = wleUIEdObjIsChildSelected(children[i]);

			editorObjectDeref(children[i]);
			if (ret)
				break;
		}
	}
	return ret;
}

static void wleUIEdObjNodeExpanded(UITreeNode *node, UserData unused)
{
	EditorObject *edObj = node->contents;

	if (edObj->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(edObj->obj);
		if (tracker)
			tracker->open = 1;
	}

	wleUITrackerTreeGetWidth(NULL);
}

static void wleUIEdObjNodeCollapsed(UITreeNode *node, UserData unused)
{
	EditorObject *edObj = node->contents;

	if (edObj)
	{
		if (editState.mode != EditSelectParent)
		{
			editorObjectRef(edObj);
			if (wleUIEdObjIsChildSelected(edObj))
				edObjSelect(edObj, true, true);
			editorObjectDeref(edObj);
		}

		if (edObj->type->objType == EDTYPE_TRACKER)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(edObj->obj);
			if (tracker)
				tracker->open = 0;
		}
	}

	wleUITrackerTreeGetWidth(NULL);
}

static void wleUIEdObjNodeFree(UITreeNode *node)
{
	EditorObject *edObj = node->contents;
	EditorObject *parentObj = NULL;

	if (!edObj)
		return;

	if (edObj->type && edObj->type->parentFunc)
		parentObj = edObj->type->parentFunc(edObj);
	if (parentObj)
		editorObjectRef(parentObj);

	wleUITrackerTreeRemoveEdObjNodeAssoc(edObj);
	if (parentObj && parentObj->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle((TrackerHandle*) parentObj->obj);
		if (tracker)
			tracker->open = 0;
	}
	editorObjectDeref(edObj);
	if (parentObj)
		editorObjectDeref(parentObj);
}

/******
* This function returns whether a particular TrackerHandle should be included or excluded
* from the tracker tree.
* PARAMS:
*   handle - TrackerHandle to check
* RETURNS:
*   bool indicating whether the TrackerHandle should be included in the tree.
******/
bool wleUITrackerNodeFilterCheck(TrackerHandle *handle)
{
	if (!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowHiddenTrackers", 1) && wleTrackerIsHidden(handle))
		return false;
	if (!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowFrozenTrackers", 1) && wleTrackerIsFrozen(handle))
		return false;

	return true;
}

static void wleUITrackerViewGroupsClicked(UIButton *button, UITreeNode *node)
{
	UITreeRefreshNode *currNode;

	if (editorUIState->trackerTreeUI.activeScopeTracker)
	{
		trackerHandleDestroy(editorUIState->trackerTreeUI.activeScopeTracker);
		editorUIState->trackerTreeUI.activeScopeTracker = NULL;
	}

	editorUIState->showingLogicalTree = false;
	editorUIState->showingLogicalNames = false;

	assert(node && editorUIState->trackerTreeUI.currRefreshNode);
	currNode = *editorUIState->trackerTreeUI.currRefreshNode;
	if (currNode)
		ui_TreeRefreshNodeDestroy(currNode);
	*(editorUIState->trackerTreeUI.currRefreshNode) = ui_TreeRefreshNodeCreate(node);
	editorUIState->trackerTreeUI.currRefreshNode = &editorUIState->trackerTreeUI.topRefreshNode;
	emQueueFunctionCall(wleUITrackerTreeRefresh, *(editorUIState->trackerTreeUI.currRefreshNode));
}

static void wleUITrackerViewScopeClicked(UIButton *button, UITreeNode *node)
{
	EditorObject *obj = (EditorObject*) node->contents;
	EditorObject *activeObj = NULL;
	TrackerHandle *handle = NULL;
	UITreeNode *topNode = NULL;
	UITreeRefreshNode *currNode;
	UITreeRefreshNode *parentNode;

	if (editorUIState->trackerTreeUI.activeScopeTracker)
		topNode = editorUIState->trackerTreeUI.trackerTree->root.children[0];

	// set active scope tracker handle
	if (editorUIState->trackerTreeUI.activeScopeTracker)
	{
		trackerHandleDestroy(editorUIState->trackerTreeUI.activeScopeTracker);
		editorUIState->trackerTreeUI.activeScopeTracker = NULL;
	}
	if (obj->type->objType == EDTYPE_TRACKER)
		handle = trackerHandleCopy(obj->obj);
	else if (obj->type->objType == EDTYPE_LAYER)
		handle = trackerHandleCreate(layerGetTracker(obj->obj));
	editorUIState->trackerTreeUI.activeScopeTracker = handle;
	editorUIState->showingLogicalTree = false;
	editorUIState->showingLogicalNames = true;

	if (!editorUIState->trackerTreeUI.topRefreshNode)
		editorUIState->trackerTreeUI.topRefreshNode = ui_TreeRefreshNodeCreate(&editorUIState->trackerTreeUI.trackerTree->root);

	assert(editorUIState->trackerTreeUI.currRefreshNode);
	currNode = *editorUIState->trackerTreeUI.currRefreshNode;
	assert(currNode);
	ui_TreeRefreshNodeDestroy(currNode);
	*editorUIState->trackerTreeUI.currRefreshNode = topNode ? ui_TreeRefreshNodeCreate(topNode) : ui_TreeRefreshNodeCreate(&editorUIState->trackerTreeUI.trackerTree->root);
	currNode = *editorUIState->trackerTreeUI.currRefreshNode;
	editorUIState->trackerTreeUI.currRefreshNode = wleUITrackerTreeGetRefreshNode(currNode, topNode ? topNode : &editorUIState->trackerTreeUI.trackerTree->root, node, &parentNode);
	assert(editorUIState->trackerTreeUI.currRefreshNode);
	emQueueFunctionCall(wleUITrackerTreeRefresh, parentNode);
}

static void wleUITrackerViewMapScopeClicked(UIButton *button, UITreeNode *node)
{
	UITreeRefreshNode *currNode = *editorUIState->trackerTreeUI.currRefreshNode;

	// set active scope tracker handle
	if (editorUIState->trackerTreeUI.activeScopeTracker)
	{
		trackerHandleDestroy(editorUIState->trackerTreeUI.activeScopeTracker);
		editorUIState->trackerTreeUI.activeScopeTracker = NULL;
	}

	editorUIState->showingLogicalNames = true;

	assert(currNode);
	ui_TreeRefreshNodeDestroy(currNode);
	*editorUIState->trackerTreeUI.currRefreshNode = ui_TreeRefreshNodeCreate(node);
	editorUIState->trackerTreeUI.currRefreshNode = &editorUIState->trackerTreeUI.topRefreshNode;
	emQueueFunctionCall(wleUITrackerTreeRefresh, editorUIState->trackerTreeUI.topRefreshNode);
}

static void wleUITrackerViewUpScopeClicked(UIButton *button, UITreeNode *node)
{
	EditorObject *obj = (EditorObject*) node->contents;
	EditorObject *upScopeObj = NULL;
	GroupTracker *tracker;
	UITreeRefreshNode *currNode = *editorUIState->trackerTreeUI.currRefreshNode;

	assert(obj->type->objType == EDTYPE_TRACKER);
	tracker = trackerFromTrackerHandle(obj->obj);
	tracker = trackerGetScopeTracker(tracker);

	// set active scope tracker handle
	if (editorUIState->trackerTreeUI.activeScopeTracker)
		trackerHandleDestroy(editorUIState->trackerTreeUI.activeScopeTracker);
	editorUIState->trackerTreeUI.activeScopeTracker = (tracker && tracker->parent) ? trackerHandleCreate(tracker) : NULL;

	if (tracker && tracker->parent)
		upScopeObj = editorObjectCreate(trackerHandleCreate(tracker), tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER);

	editorUIState->showingLogicalNames = true;

	assert(currNode);
	ui_TreeRefreshNodeDestroy(currNode);
	*editorUIState->trackerTreeUI.currRefreshNode = ui_TreeRefreshNodeCreate(node);
	emQueueFunctionCall(wleUITrackerTreeRefreshForEdObj, upScopeObj);
}

const char *wleUIEdObjGetDisplayText(EditorObject *object)
{
	EditorObject **selected_list = object ? edObjSelectionGet(object->type->objType) : NULL;
	static char nodeText[256];

	// trackers
	if (!object)
		return NULL;

	if (object->type->objType == EDTYPE_TRACKER || object->type->objType == EDTYPE_LAYER)
	{
		TrackerHandle *handle = object->type->objType == EDTYPE_LAYER ? trackerHandleFromTracker(layerGetTracker(object->obj)) : (TrackerHandle*)object->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(handle);
		GroupDef *def = groupDefFromTrackerHandle(handle);
		const char *groupName = NULL;
		const char *uniqueName = NULL;

		if (!tracker)
			return NULL;

		if(!editorUIState->showingLogicalNames || !groupDefNeedsUniqueName(def))
			groupName = object->name;

		if(editorUIState->showingLogicalNames && groupDefNeedsUniqueName(def))
			uniqueName = wleTrackerHandleToUniqueName(editorUIState->trackerTreeUI.activeScopeTracker, handle);

		if(uniqueName)
			sprintf(nodeText, "%s", uniqueName);
		else if(groupName)
			sprintf(nodeText, "%s", groupName);
		else
			sprintf(nodeText, "%s", object->name);
	}
	else
		sprintf(nodeText, "%s", object->name);

	return nodeText;
}

const char *wleUIEdObjGetDisplayComment(EditorObject *object)
{
	EditorObject **selected_list = object ? edObjSelectionGet(object->type->objType) : NULL;

	// trackers
	if (!object)
		return NULL;

	if (object->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		WleEncObjSubHandle *subHandle = object->obj;
		WorldActorProperties *actor = wleEncounterActorFromHandle(subHandle, NULL);
		WorldEncounterProperties *encounterProperties = wleEncounterFromHandle(subHandle);
		EncounterTemplate *pTemplate = encounterProperties ? GET_REF(encounterProperties->hTemplate) : NULL;
		EncounterActorProperties *pTemplateActor = encounterTemplate_GetActorFromWorldActor(pTemplate, encounterProperties, actor);

		if(pTemplateActor)
			return pTemplateActor->nameProps.pchComments;
	}

	return NULL;
}


static void wleUITrackerNodeDisplay(UITreeNode *node, UserData unused, UI_MY_ARGS, F32 z)
{
	EditorObject *object = node->contents;
	UIStyleFont *font = NULL;
	EditorObject **selected_list = object ? edObjSelectionGet(object->type->objType) : NULL;
	GfxFont *drawContext;
	AtlasTex *spr;
	const char *objText;
	const char *objComment;
	char nodeText[256];
	bool object_selected = false;
	int i;
	bool parent_locked;

	// trackers
	if (!object)
		return;

	objText = wleUIEdObjGetDisplayText(object);
	objComment = wleUIEdObjGetDisplayComment(object);

	parent_locked = (layerGetLocked((ZoneMapLayer*)object->context) == 3);

	if (object->type->objType == EDTYPE_TRACKER || object->type->objType == EDTYPE_LAYER)
	{
		TrackerHandle *handle = object->type->objType == EDTYPE_LAYER ? trackerHandleFromTracker(layerGetTracker(object->obj)) : (TrackerHandle*)object->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(handle);
		GroupDef *def = groupDefFromTrackerHandle(handle);

		if (!tracker)
			return;

		{
			// Is parent locked?
			GroupTracker *parent = tracker;
			while (parent = parent->parent)
			{
				if (!parent->def)
					break;

				if (groupIsPublic(parent->def))
				{
					parent_locked = groupIsEditable(parent->def);
					break;
				}
			}
		}

		if (layerGetLocked(object->type->objType == EDTYPE_LAYER ? object->obj : object->context) == 3)
			font = GET_REF(editorUIState->trackerTreeUI.fontNormalLocked);
		else
			font = GET_REF(editorUIState->trackerTreeUI.fontNormalUnlocked);

		if (tracker->selected)
			font = GET_REF(editorUIState->trackerTreeUI.fontSelected);
		else if (tracker->frozen)
			font = GET_REF(editorUIState->trackerTreeUI.fontFrozen);
		else if (editorUIState->showingLogicalNames && groupDefNeedsUniqueName(def))
			font = GET_REF(editorUIState->trackerTreeUI.fontUniqueName);
		else if (def && groupIsObjLib(def))
		{
			if (!groupIsPublic(def))
			{
				if (groupIsEditable(def))
					font = GET_REF(editorUIState->trackerTreeUI.fontPrivateLocked);
				else
					font = GET_REF(editorUIState->trackerTreeUI.fontPrivateUnlocked);
			}
			else
			{
				if (groupIsEditable(def))
					font = GET_REF(editorUIState->trackerTreeUI.fontLibraryLocked);
				else
					font = GET_REF(editorUIState->trackerTreeUI.fontLibraryUnlocked);
			}
		}

		ui_StyleFontUse(font, node->tree->selected == node, UI_WIDGET(node->tree)->state);
		sprintf(nodeText, "%s%s%s%s%s", (editState.defaultParent && trackerHandleComp(handle, editState.defaultParent) == 0) ? "PARENT >> " : "",
			tracker->invisible ? "(" : "", tracker->frozen ? "*FROZEN* " : "", objText, tracker->invisible ? ")" : "");
		if (tracker->def && EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowDefUIDs", 0) != 0)
		{
			if (tracker->def->root_id != 0)
				strcatf(nodeText, " (%d in %d)", tracker->def->name_uid, tracker->def->root_id);
			else
				strcatf(nodeText, " (%d)", tracker->def->name_uid);
		}
	}
	else
	{
		for (i = 0; i < eaSize(&selected_list); i++)
		{
			if (edObjCompareForUI(object, selected_list[i]) == 0)
			{
				object_selected = true;
				break;
			}
		}

		if (object_selected)
			font = GET_REF(editorUIState->trackerTreeUI.fontSelected);
		else if (layerGetLocked((ZoneMapLayer*)object->context) == 3)
			font = GET_REF(editorUIState->trackerTreeUI.fontNormalLocked);
		else
			font = GET_REF(editorUIState->trackerTreeUI.fontNormalUnlocked);

		ui_StyleFontUse(font, node->tree->selected == node, UI_WIDGET(node->tree)->state);

		sprintf(nodeText, "%s", objText);
	}

	// print node text
	gfxfont_Printf(x + 13 * scale, y + h / 2, z, scale, scale, CENTER_Y, "%s", nodeText);

	// calculate width of font
	if (!font)
		font = GET_REF(g_ui_State.font);
	drawContext = font ? GET_REF(font->hFace) : NULL;

	// print node comment
	if(EMPTY_TO_NULL(objComment) && drawContext)
	{
		// get width of node text
		F32 fWidth = gfxfont_StringWidthf(drawContext, 1, 1, "%s", nodeText) + 13;

		// print comment
		ui_StyleFontUse(font, false, kWidgetModifier_None);
		gfxfont_Printf(x + fWidth + 13, y + h / 2, z, scale, scale, CENTER_Y, ":: %s", objComment);
	}

	// re-calculate width of entire string
	if (drawContext) {
		if(EMPTY_TO_NULL(objComment))
		{
			node->displayData = (void*)(intptr_t) (gfxfont_StringWidthf(drawContext, 1, 1, "%s", nodeText) + gfxfont_StringWidthf(drawContext, 1, 1, "%s", objComment) + 26);
		}
		else
		{
			node->displayData = (void*)(intptr_t) (gfxfont_StringWidthf(drawContext, 1, 1, "%s", nodeText) + 13);
		}
	}

	// render editing icon
	if (parent_locked)
	{
		if (!object->type->movementEnableFunc || 
			!object->type->movementEnableFunc(object, edObjHarnessGetGizmo()))
			spr = atlasLoadTexture("eui_gimme_readonly");
		else
			spr = atlasLoadTexture("eui_gimme_ok");
	}
	else
		spr = atlasLoadTexture("eui_gimme_readonly");
	display_sprite(spr, x, y, z, scale, scale, 0xFFFFFFFF);
}

static char *wleUITrackerNodeTooltip(UITreeNode *node, UserData unused)
{
	EditorObject *object = node->contents;
	const char *objText;
	const char *objComment;
	char buf[1024];

	// trackers
	if (!object)
		return NULL;

	if (EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "HideTooltips", 0) != 0)
		return NULL;

	objText = wleUIEdObjGetDisplayText(object);
	objComment = wleUIEdObjGetDisplayComment(object);

	if (object->type->objType == EDTYPE_TRACKER || object->type->objType == EDTYPE_LAYER)
	{
		TrackerHandle *handle = object->type->objType == EDTYPE_LAYER ? trackerHandleFromTracker(layerGetTracker(object->obj)) : (TrackerHandle*)object->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(handle);
		GroupDef *def = groupDefFromTrackerHandle(handle);
		bool locked = false;
		UIStyleFont *font = NULL;
		char font_color[128];
		const char *desc = NULL;

		if (!tracker || !def)
			return NULL;

		if (def->root_id != 0)
			sprintf(buf, "%s (%d in parent %d)", def->name_str, def->name_uid, def->root_id);
		else
			sprintf(buf, "%s (%d)", def->name_str, def->name_uid);

		if (def->is_new)
			strcat(buf, " NEW");

		if (def && groupIsObjLib(def))
		{
			if (!groupIsPublic(def))
			{
				desc = "Private group in Object Library";
				if (groupIsEditable(def))
				{
					font = GET_REF(editorUIState->trackerTreeUI.fontPrivateLocked);
					locked = true;
				}
				else
				{
					font = GET_REF(editorUIState->trackerTreeUI.fontPrivateUnlocked);
					locked = false;
				}
			}
			else
			{
				desc = "Public group in Object Library";
				if (groupIsEditable(def))
				{
					font = GET_REF(editorUIState->trackerTreeUI.fontLibraryLocked);
					locked = true;
				}
				else
				{
					font = GET_REF(editorUIState->trackerTreeUI.fontLibraryUnlocked);
					locked = false;
				}
			}
		}
		else
		{
			desc = "Layer group on map";
			if (layerGetLocked(object->type->objType == EDTYPE_LAYER ? object->obj : object->context) == 3)
			{
				font = GET_REF(editorUIState->trackerTreeUI.fontNormalLocked);
				locked = true;
			}
			else
			{
				font = GET_REF(editorUIState->trackerTreeUI.fontNormalUnlocked);
				locked = false;
			}
		}

		strcat(buf, "<br>");
		if (font)
		{
			sprintf(font_color, "<font color=\"#%08X\">", ui_StyleFontGetColorValue(font));
			strcat(buf, font_color);
		}
		if (locked)
			strcat(buf, "<Editable> ");
		else
			strcat(buf, "<Not Editable> ");
		strcat(buf, desc);
		if (font)
			strcat(buf, "</font>");

		if (tracker->frozen)
			strcat(buf, "<br><b><FROZEN></b>");
	}
	else
	{
		sprintf(buf, "%s", objText);
	}
	return strdup(buf);
}

static void wleUIEdObjNodeWidgetsCreate(UITreeNode *node, EditorObject *edObj)
{
	UIButton *button;

	if (edObj->type->objType == EDTYPE_TRACKER && !editorUIState->showingLogicalTree && (editorUIState->trackerTreeUI.activeScopeTracker && trackerHandleComp(editorUIState->trackerTreeUI.activeScopeTracker, edObj->obj) == 0))
	{
		GroupTracker *tracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);

		button = ui_ButtonCreate("Groups", 0, 0, wleUITrackerViewGroupsClicked, node);
		button->widget.scale = 0.9;
		button->widget.height = 1;
		button->widget.heightUnit = UIUnitPercentage;
		button->widget.offsetFrom = UITopRight;
		ui_WidgetGroupAdd(&node->widgets, UI_WIDGET(button));

		if (tracker)
		{
			button = ui_ButtonCreate("Map Scope", elUINextX(button), 0, wleUITrackerViewMapScopeClicked, node);
			button->widget.scale = 0.9;
			button->widget.height = 1;
			button->widget.heightUnit = UIUnitPercentage;
			button->widget.offsetFrom = UITopRight;
			ui_WidgetGroupAdd(&node->widgets, UI_WIDGET(button));

			button = ui_ButtonCreate("Up", elUINextX(button), 0, wleUITrackerViewUpScopeClicked, node);
			button->widget.scale = 0.9;
			button->widget.height = 1;
			button->widget.heightUnit = UIUnitPercentage;
			button->widget.offsetFrom = UITopRight;
			ui_WidgetGroupAdd(&node->widgets, UI_WIDGET(button));
		}
	}
	else if (edObj->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(edObj->obj);
		if (tracker && tracker->def && tracker->def->path_to_name)
		{
			button = ui_ButtonCreate("Scope", 0, 0, wleUITrackerViewScopeClicked, node);
			button->widget.scale = 0.9;
			button->widget.height = 1;
			button->widget.heightUnit = UIUnitPercentage;
			button->widget.offsetFrom = UITopRight;
			ui_WidgetGroupAdd(&node->widgets, UI_WIDGET(button));
		}
	}
/*
	else if (edObj->type->objType == EDTYPE_LOGICAL_GROUP)
	{

		GroupTracker *tracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);

		button = ui_ButtonCreate("Groups", 0, 0, wleUITrackerViewGroupsClicked, node);
		button->widget.height = 1;
		button->widget.heightUnit = UIUnitPercentage;
		button->widget.offsetFrom = UITopRight;
		ui_WidgetGroupAdd(&node->widgets, UI_WIDGET(button));

		if (tracker)
		{
			button = ui_ButtonCreate("Map Scope", elUINextX(button), 0, wleUITrackerViewMapScopeClicked, node);
			button->widget.height = 1;
			button->widget.heightUnit = UIUnitPercentage;
			button->widget.offsetFrom = UITopRight;
			ui_WidgetGroupAdd(&node->widgets, UI_WIDGET(button));

			button = ui_ButtonCreate("Up", elUINextX(button), 0, wleUITrackerViewUpScopeClicked, node);
			button->widget.height = 1;
			button->widget.heightUnit = UIUnitPercentage;
			button->widget.offsetFrom = UITopRight;
			ui_WidgetGroupAdd(&node->widgets, UI_WIDGET(button));
		}
	}*/
}

static UITreeNode *wleUIEdObjNodeCreate(UITree *tree, EditorObject *edObj)
{
	EditorObject **fillCheck = NULL;
	bool hasChildren = false;
	UITreeNode *node;
	U32 crc;
	editorObjectRef(edObj);

	// get CRC
	if (edObj->type->crcFunc)
		crc = edObj->type->crcFunc(edObj);
	else
		crc = *(U32 *)&edObj->obj;

	// determine whether this object has children
	if (edObj->type->childFunc)
		edObj->type->childFunc(edObj, &fillCheck);
	eaForEach(&fillCheck, editorObjectRef);
	if (eaSize(&fillCheck) > 0)
		hasChildren = true;

	// create node
	node = ui_TreeNodeCreate(tree, crc, NULL, 
		edObj, hasChildren ? wleUITrackerNodeFill : NULL, edObj, wleUITrackerNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
	node->tooltipF = wleUITrackerNodeTooltip;
	wleUIEdObjNodeWidgetsCreate(node, edObj);
	wleUITrackerTreeAddEdObjNodeAssoc(edObj, node);
	ui_TreeNodeSetExpandCallback(node, wleUIEdObjNodeExpanded, NULL);
	ui_TreeNodeSetCollapseCallback(node, wleUIEdObjNodeCollapsed, NULL);
	ui_TreeNodeSetFreeCallback(node, wleUIEdObjNodeFree);

	eaDestroyEx(&fillCheck, editorObjectDeref);
	editorObjectDeref(edObj);

	return node;
}

static void wleUITrackerNodeFill(UITreeNode *parent, EditorObject *object)
{
	int i;
	EditorObject **children = NULL;

	if (object->type->childFunc)
		object->type->childFunc(object, &children);

	if (object->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(object->obj);
		if (tracker)
			tracker->open = 1;
	}

	for (i = 0; i < eaSize(&children); i++)
	{
		EditorObject *edObj = children[i];

		editorObjectRef(edObj);

		// do tracker tree filtering here
		if (edObj->type->objType == EDTYPE_TRACKER && !wleUITrackerNodeFilterCheck((TrackerHandle*) edObj->obj))
		{
			editorObjectDeref(edObj);
			continue;
		}
		else
		{
			UITreeNode *childNode = wleUIEdObjNodeCreate(parent->tree, edObj);

			ui_TreeNodeAddChild(parent, childNode);

			// prevent child trackers of logical groups from showing their children; this is causing
			// problems in which an object can appear twice in the tracker tree
			if (object->type->objType == EDTYPE_LOGICAL_GROUP && edObj->type->objType == EDTYPE_TRACKER)
				childNode->fillF = childNode->fillData = NULL;
		}
	}

	eaDestroy(&children);
}

// logical group tree callbacks
typedef enum WleUILogicalNodeType
{
	WLE_UI_LOGICALNODE_UNGROUPED			= 1,
	WLE_UI_LOGICALNODE_SPAWN_POINTS,
	WLE_UI_LOGICALNODE_NAMED_POINTS,
	WLE_UI_LOGICALNODE_PATROLS,
	WLE_UI_LOGICALNODE_ENCOUNTERS,
	WLE_UI_LOGICALNODE_NAMED_VOLUMES,
	WLE_UI_LOGICALNODE_INTERACTABLES,
	WLE_UI_LOGICALNODE_GROUPED,
} WleUILogicalNodeType;
static void wleUILogicalGroupDisplay(UITreeNode *node, UserData unused, UI_MY_ARGS, F32 z)
{
	EditorObject *edObj = (EditorObject*) node->contents;
	WorldLogicalGroup *group = edObj ? (WorldLogicalGroup*) edObj->obj : NULL;
	GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
	WorldScope *scope;
	char *scopeName;
	UIStyleFont *font = NULL;
	GfxFont *drawContext;

	assert(group);
	scope = group->common_data.closest_scope;
	while (scope && scope->tracker != scopeTracker)
		scope = scope->parent_scope;
	assert(scope);

	if (!stashFindPointer(scope->obj_to_name, group, &scopeName))
		scopeName = "NAME NOT FOUND";
	if (group->selected)
		font = GET_REF(editorUIState->trackerTreeUI.fontSelected);
	else
		font = GET_REF(g_ui_State.font);

	ui_StyleFontUse(font, node->tree->selected == node, UI_WIDGET(node->tree)->state);
	gfxfont_Printf(x, y + h / 2, z, scale, scale, CENTER_Y, "%s", scopeName);

	// calculate width
	drawContext = font ? GET_REF(font->hFace) : NULL;
	if (drawContext)
		node->displayData = (void*)(intptr_t) (gfxfont_StringWidthf(drawContext, 1, 1, "%s", scopeName) + 13);
}

static void wleUILogicalNodeDisplay(UITreeNode *node, UserData unused, UI_MY_ARGS, F32 z)
{
	WleUILogicalNodeType type = node->crc;
	UIStyleFont *font = NULL;
	GfxFont *drawContext;
	char nodeText[256];

	font = GET_REF(g_ui_State.font);

	ui_StyleFontUse(font, node->tree->selected == node, UI_WIDGET(node->tree)->state);
	switch (type)
	{
		xcase WLE_UI_LOGICALNODE_UNGROUPED: 
			sprintf(nodeText, "Ungrouped Objects");
		xcase WLE_UI_LOGICALNODE_SPAWN_POINTS:
			sprintf(nodeText, "Spawn Points");
		xcase WLE_UI_LOGICALNODE_NAMED_POINTS:
			sprintf(nodeText, "Named Points");
		xcase WLE_UI_LOGICALNODE_PATROLS:
			sprintf(nodeText, "Patrol Routes");
		xcase WLE_UI_LOGICALNODE_ENCOUNTERS:
			sprintf(nodeText, "Encounters");
		xcase WLE_UI_LOGICALNODE_NAMED_VOLUMES:
			sprintf(nodeText, "Volumes");
		xcase WLE_UI_LOGICALNODE_INTERACTABLES:
			sprintf(nodeText, "Interactables");
		xcase WLE_UI_LOGICALNODE_GROUPED:
			sprintf(nodeText, "Logical Groups");
		xdefault:
			sprintf(nodeText, "UNDEFINED");
	}
	gfxfont_Printf(x, y + h / 2, z, scale, scale, CENTER_Y, "%s", nodeText);

	// calculate width
	drawContext = font ? GET_REF(font->hFace) : NULL;
	if (drawContext)
		node->displayData = (void*)(intptr_t) (gfxfont_StringWidthf(drawContext, 1, 1, "%s", nodeText) + 13);
}

static void wleUILogicalNodeFill(UITreeNode *parent, UserData unused)
{
	GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
	WorldScope *scope = scopeTracker ? scopeTracker->closest_scope : (WorldScope*) zmapGetScope(NULL);
	WorldZoneMapScope *zmapScope = zmapGetScope(NULL);
	UITreeNode *node;
	WleUILogicalNodeType type = parent->crc;

	assert(zmapScope);
	if (type == WLE_UI_LOGICALNODE_GROUPED)
	{
		EditorObject *edObj;
		char *zmapScopeName;
		char *groupName;
		int i;

		// fill top-level logical group nodes
		assert(zmapScope);
		for (i = 0; i < eaSize(&zmapScope->groups); i++)
		{
			// only display unparented groups that are below the active scope tracker
			if ((!zmapScope->groups[i]->common_data.parent_group || !stashFindPointer(scope->obj_to_name, zmapScope->groups[i]->common_data.parent_group, NULL)) && scope->obj_to_name && stashFindPointer(zmapScope->scope.obj_to_name, zmapScope->groups[i], &zmapScopeName) && stashFindPointer(scope->obj_to_name, zmapScope->groups[i], &groupName))
			{
				edObj = editorObjectCreate(strdup(zmapScopeName), groupName, zmapScope->groups[i]->common_data.layer, EDTYPE_LOGICAL_GROUP);
				editorObjectRef(edObj);
				ui_TreeNodeAddChild(parent, wleUIEdObjNodeCreate(parent->tree, edObj));
			}
		}
	}
	else if (type == WLE_UI_LOGICALNODE_UNGROUPED)
	{
		node = ui_TreeNodeCreate(parent->tree, WLE_UI_LOGICALNODE_SPAWN_POINTS, NULL, NULL, wleUILogicalNodeFill, NULL, wleUILogicalNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
		ui_TreeNodeAddChild(parent, node);
		node = ui_TreeNodeCreate(parent->tree, WLE_UI_LOGICALNODE_NAMED_VOLUMES, NULL, NULL, wleUILogicalNodeFill, NULL, wleUILogicalNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
		ui_TreeNodeAddChild(parent, node);
		node = ui_TreeNodeCreate(parent->tree, WLE_UI_LOGICALNODE_INTERACTABLES, NULL, NULL, wleUILogicalNodeFill, NULL, wleUILogicalNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
		ui_TreeNodeAddChild(parent, node);
		node = ui_TreeNodeCreate(parent->tree, WLE_UI_LOGICALNODE_NAMED_POINTS, NULL, NULL, wleUILogicalNodeFill, NULL, wleUILogicalNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
		ui_TreeNodeAddChild(parent, node);
		node = ui_TreeNodeCreate(parent->tree, WLE_UI_LOGICALNODE_PATROLS, NULL, NULL, wleUILogicalNodeFill, NULL, wleUILogicalNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
		ui_TreeNodeAddChild(parent, node);
		node = ui_TreeNodeCreate(parent->tree, WLE_UI_LOGICALNODE_ENCOUNTERS, NULL, NULL, wleUILogicalNodeFill, NULL, wleUILogicalNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
		ui_TreeNodeAddChild(parent, node);
	}
	else
	{
		WorldEncounterObject **objects = NULL;
		int i;

		switch (type)
		{
			xcase WLE_UI_LOGICALNODE_SPAWN_POINTS:
				objects = (WorldEncounterObject**) zmapScope->spawn_points;
			xcase WLE_UI_LOGICALNODE_NAMED_VOLUMES:
				objects = (WorldEncounterObject**) zmapScope->named_volumes;
			xcase WLE_UI_LOGICALNODE_INTERACTABLES:
				objects = (WorldEncounterObject**) zmapScope->interactables;
			xcase WLE_UI_LOGICALNODE_NAMED_POINTS:
				objects = (WorldEncounterObject**) zmapScope->named_points;
			xcase WLE_UI_LOGICALNODE_PATROLS:
				objects = (WorldEncounterObject**) zmapScope->patrol_routes;
			xcase WLE_UI_LOGICALNODE_ENCOUNTERS:
				objects = (WorldEncounterObject**) zmapScope->encounters;
		}

		for (i = 0; i < eaSize(&objects); i++)
		{
			if (objects[i]->tracker && !objects[i]->parent_group && stashFindPointer(scope->obj_to_name, objects[i], NULL))
			{
				EditorObject *edObj = editorObjectCreate(trackerHandleCreate(objects[i]->tracker), objects[i]->tracker->def->name_str, objects[i]->tracker->parent_layer, EDTYPE_TRACKER);
				UITreeNode *childNode;
				
				editorObjectRef(edObj);
				childNode = wleUIEdObjNodeCreate(parent->tree, edObj);
				ui_TreeNodeAddChild(parent, childNode);

				// remove fill function/data so that children aren't displayed - this is causing problems because
				// of nested encounter objects, which allows an object to appear twice in the tracker tree
				childNode->fillF = childNode->fillData = NULL;
			}
		}
	}
}

static void wleUITrackerNodeFillRoot(UITreeNode *root, void *temp)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	// create layer nodes
	if (editorUIState->showingLogicalTree)
	{
		UITreeNode *node;

		// create ungrouped objects node
		node = ui_TreeNodeCreate(root->tree, WLE_UI_LOGICALNODE_GROUPED, NULL, NULL, wleUILogicalNodeFill, NULL, wleUILogicalNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
		ui_TreeNodeAddChild(root, node);
		node = ui_TreeNodeCreate(root->tree, WLE_UI_LOGICALNODE_UNGROUPED, NULL, NULL, wleUILogicalNodeFill, NULL, wleUILogicalNodeDisplay, NULL, WLE_TRACKER_TREE_NODE_HEIGHT);
		ui_TreeNodeAddChild(root, node);
	}
	else if (!editorUIState->trackerTreeUI.activeScopeTracker)
	{
		for (i = 0; i < zmapGetLayerCount(NULL); ++i)
		{
			GroupTracker *tracker;
			TrackerHandle *tempHandle;
			ZoneMapLayer *layer = zmapGetLayer(NULL, i);
			assert(layer);

			if (layer->layer_mode == LAYER_MODE_STREAMING)
				continue;

			tracker = layerGetTracker(layer);
			tempHandle = trackerHandleFromTracker(tracker);
			if (tempHandle && wleUITrackerNodeFilterCheck(tempHandle))
			{
				EditorObject *edObj = editorObjectCreate(layer, tracker->def->name_str, layer, EDTYPE_LAYER);
				editorObjectRef(edObj);
				ui_TreeNodeAddChild(root, wleUIEdObjNodeCreate(root->tree, edObj));
			}
		}
	}
	else
	{
		// use active scope tracker as topmost node
		GroupTracker *tracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);

		if (tracker)
		{
			EditorObject *edObj = editorObjectCreate(trackerHandleCopy(editorUIState->trackerTreeUI.activeScopeTracker), tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER);
			editorObjectRef(edObj);
			ui_TreeNodeAddChild(root, wleUIEdObjNodeCreate(root->tree, edObj));
		}
	}

	PERFINFO_AUTO_STOP();
}

static void wleUITrackerNodeActivate(UITree *trackerTree, UserData unused)
{
	UITreeNode *selected = ui_TreeGetSelected(trackerTree);
	if (selected)
	{
		EditorObject *object = selected->contents;
		TrackerHandle *handle = (object && object->type->objType == EDTYPE_TRACKER) ? (TrackerHandle*)object->obj : NULL;

		// expand non-object nodes
		if (!object)
		{
			if (selected->open)
				ui_TreeNodeCollapse(selected);
			else
				ui_TreeNodeExpand(selected);
		}
		else
		{
			wleUIEdObjRenameDialog(object);
			wleUITrackerNodeSelect(trackerTree, NULL);
		}
	}
}

static void wleUITrackerNodeSelect(UITree *trackerTree, void *stuff)
{
	UITreeNode *selected = ui_TreeGetSelected(trackerTree);
	UITreeNode *oldLastSelected = editorUIState->trackerTreeUI.lastSelected;
	EditorObject **selectionList = NULL;
	EditorObject *object;
	bool selecting = false;
	bool shiftDown = inpLevelPeek(INP_SHIFT) && editState.mode == EditNormal;
	int i;

	if (!selected)
	{
		editorUIState->trackerTreeUI.lastSelected = NULL;
		return;
	}

	object = selected->contents;

	if (!object)
		return;

	// shift-click selection
	if (shiftDown && editorUIState->trackerTreeUI.lastSelected && editorUIState->trackerTreeUI.lastSelected != selected)
	{
		UITreeNode *parentNode = ui_TreeNodeFindParent(trackerTree, selected);
		for (i = 0; i < eaSize(&parentNode->children); i++)
		{
			UITreeNode *child = parentNode->children[i];
			bool isEndpoint = (child == editorUIState->trackerTreeUI.lastSelected || child == selected);
			if (selecting || isEndpoint)
			{
				if (isEndpoint)
				{
					selecting = !selecting;
				}
				eaPush(&selectionList, (EditorObject*) child->contents);

				//A shift-click will only start & stop selecting once, so if it reaches here, it's stopped selecting & the rest of the list should be ignored.
				if (!selecting)
				{
					break;
				}
			}
		}
		// if one of the selection endpoints were not found, the shift-click selection is invalid and nothing
		// is, in fact, being selected
		if (selecting)
			eaClear(&selectionList);
	}
	if (eaSize(&selectionList) == 0)
		eaPush(&selectionList, object);

	editorUIState->trackerTreeUI.lastSelected = trackerTree->selected = NULL;
	EditUndoBeginGroup(edObjGetUndoStack());
	if (shiftDown && !inpLevelPeek(INP_CONTROL) && !inpLevelPeek(INP_ALT))
		edObjSelectionClearEx(EDTYPE_NONE, false);

	if (inpLevelPeek(INP_ALT))
	{
		edObjDeselectList(selectionList);
	}
	else if (shiftDown || inpLevelPeek(INP_CONTROL))
	{
		edObjSelectList(selectionList, true, false);
	}
	else if (!shiftDown)
	{
		edObjSelectList(selectionList, false, false);
	}

	EditUndoEndGroup(edObjGetUndoStack());
	if (shiftDown)
	{
		// highlight the clicked node
		if (!inpLevelPeek(INP_ALT))
			edObjSelect(object, true, false);

		// reset the last selected node so that further shift clicking works off the original node
		editorUIState->trackerTreeUI.lastSelected = oldLastSelected;
	}
	eaDestroy(&selectionList);
	wleUISearchClearUniqueness();
}

// drag and drop
static void wleUITrackerTreeDrag(UITree *tree, 
								 UITreeNode *node, 
								 UITreeNode *dragFromParent, 
								 UITreeNode *dragToParent, 
								 int dragToIndex, 
								 UserData dragData)
{
	EditorObject *target = node->contents;
	EditorObject *dest = dragToParent->contents;

	if (!target || !dest)
		return;
	else if (target->type->objType == EDTYPE_LAYER)
		emStatusPrintf("Cannot move layers!");
	else if (target->type->objType == EDTYPE_PATROL_POINT)
	{
		WleEncObjSubHandle *targetHandle = target->obj;
		if (dest->type->objType != EDTYPE_TRACKER)
			return;
		if (trackerHandleComp(targetHandle->parentHandle, (TrackerHandle*) dest->obj))
		{
			emStatusPrintf("Cannot move patrol points to other trackers.");
			return;
		}
		wleOpMovePatrolPointToIndex(targetHandle, dragToIndex);
	}
	else if (target->type->objType != EDTYPE_TRACKER && target->type->objType != EDTYPE_LOGICAL_GROUP)
		return;
	else if (!editorUIState->showingLogicalTree)
	{
		GroupTracker *parentTracker = NULL, *tracker;
		TrackerHandle *destHandle = NULL;
		TrackerHandle **handles = NULL;

		// get drag targets
		wleSelectionGetTrackerHandles(&handles);

		// get drag destination
		tracker = trackerFromTrackerHandle(target->obj);
		if (dest->type->objType == EDTYPE_LAYER)
		{
			ZoneMapLayer *layer = dest->obj;
			parentTracker = layerGetTracker(layer);
			if (!parentTracker)
				return;
			destHandle = trackerHandleCreate(parentTracker);
		}
		else if (dest->type->objType == EDTYPE_TRACKER)
		{
			parentTracker = trackerFromTrackerHandle(dest->obj);
			destHandle = trackerHandleCopy(dest->obj);
		}

		// fix up index
		if (parentTracker == tracker->parent)
		{
			if (dragToIndex > tracker->idx_in_parent)
				dragToIndex++;
		}

		wleOpAddToGroup(destHandle, handles, dragToIndex);
		trackerHandleDestroy(destHandle);
		eaDestroy(&handles);
	}
	else if (editorUIState->showingLogicalTree && dest->type->objType == EDTYPE_LOGICAL_GROUP)
	{
		WorldScope *zmapScope = (WorldScope*) zmapGetScope(NULL);
		GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
		WorldScope *closestScope = scopeTracker ? scopeTracker->closest_scope : zmapScope;
		WorldLogicalGroup *group;
		char *parentScopeName = NULL;
		const char *childScopeName = NULL;

		assert(zmapScope);
		if (zmapScope && stashFindPointer(zmapScope->name_to_obj, dest->obj, &group))
			stashFindPointer(closestScope->obj_to_name, group, &parentScopeName);
		if (target->type->objType == EDTYPE_LOGICAL_GROUP && zmapScope && stashFindPointer(zmapScope->name_to_obj, target->obj, &group))
		{
			char *name;

			stashFindPointer(closestScope->obj_to_name, group, &name);
			childScopeName = name;
		}
		else if (target->type->objType == EDTYPE_TRACKER)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(target->obj);
			childScopeName = trackerGetUniqueScopeName(scopeTracker ? scopeTracker->def : NULL, tracker, NULL);
		}

		if (parentScopeName && childScopeName)
		{
			const char **childNames = NULL;
			eaPush(&childNames, childScopeName);
			wleOpAddToLogicalGroup(editorUIState->trackerTreeUI.activeScopeTracker, parentScopeName, childNames, dragToIndex);
			eaDestroy(&childNames);
		}
	}
}

/******
* This function ensures the tracker tree is open down to the node that matches the specified editor object,
* and that the corresponding node is closed.
* PARAMS:
*   edObj - EditorObject to ensure is viewable in the tracker tree
******/
void wleUITrackerTreeExpandToEdObj(EditorObject *edObj)
{
	EditorObject **parents = NULL;
	EditorObject *origObj = edObj;
	UITreeNode *node;
	bool exit = false;

	if (!edObj)
		return;

	editorObjectRef(origObj);
	if (node = wleUITrackerTreeGetNodeForEdObj(origObj))
	{
		editorObjectDeref(origObj);
		ui_TreeNodeCollapseAndCallback(node);
		return;
	}
	do 
	{
		if (!edObj->type->parentFunc)
			break;

		edObj = edObj->type->parentFunc(edObj);

		if (!edObj)
			break;
		editorObjectRef(edObj);
		if (node = wleUITrackerTreeGetNodeForEdObj(edObj))
		{
			int i;

			// open all parents down to specified editor object
			ui_TreeNodeExpand(node);
			for (i = eaSize(&parents) - 1; i >= 0; i--)
			{
				node = wleUITrackerTreeGetNodeForEdObj(parents[i]);
				if (node)
					ui_TreeNodeExpand(node);
				else
					break;
			}
			editorObjectDeref(edObj);
			exit = true;
		}
		else
			eaPush(&parents, edObj);

	} while(!exit);

	if (node = wleUITrackerTreeGetNodeForEdObj(origObj))
		ui_TreeNodeCollapseAndCallback(node);
	assert(origObj);
	editorObjectDeref(origObj);
	eaDestroyEx(&parents, editorObjectDeref);
}

/******
* This function highlights the UITreeNode corresponding to the specified editor object.
* PARAMS:
*   edObj - EditorObject to highlight in the tracker tree
******/
void wleUITrackerTreeHighlightEdObj(EditorObject *edObj)
{
	UITree *tree = editorUIState->trackerTreeUI.trackerTree;
	UITreeNode *node = wleUITrackerTreeGetNodeForEdObj(edObj);
	editorUIState->trackerTreeUI.lastSelected = tree->selected = node;
	if (node)
	{
		ui_TreeIteratorFree(editorUIState->trackerSearchUI.searchIterator);
		editorUIState->trackerSearchUI.searchIterator = ui_TreeIteratorCreateFromNode(node, true, true);
	}
}

/******
* This function takes a UITreeNode and and centers the tracker tree visible area on the node.
* PARAMS:
*   node - UITreeNode to center on
******/
void wleUITrackerTreeCenterOnEdObj(EditorObject *edObj)
{
	UITree *tree = editorUIState->trackerTreeUI.trackerTree;
	UITreeNode *node = wleUITrackerTreeGetNodeForEdObj(edObj);
	if (node)
	{
		editorUIState->trackerTreeUI.lastSelected = tree->selected = node;
		ui_TreeIteratorFree(editorUIState->trackerSearchUI.searchIterator);
		editorUIState->trackerSearchUI.searchIterator = ui_TreeIteratorCreateFromNode(node, true, true);
		tree->scrollToSelected = 1;
	}
	else
		editorUIState->trackerTreeUI.lastSelected = tree->selected = NULL;
}

static void wleUITrackerTreeCollapseAllClicked(UIButton *button, void *unused)
{
	int i;
	for (i = 0; i < eaSize(&editorUIState->trackerTreeUI.trackerTree->root.children); ++i)
		ui_TreeNodeCollapse(editorUIState->trackerTreeUI.trackerTree->root.children[i]);
}

static void wleUITrackerTreeDisplayModeRefresh(void)
{
	if (editorUIState)
	{
		if(editorUIState->toolbarUI.groupTreeButton)
			editorUIState->toolbarUI.groupTreeButton->down = !editorUIState->showingLogicalTree;
		if(editorUIState->toolbarUI.logicalTreeButton)
			editorUIState->toolbarUI.logicalTreeButton->down = editorUIState->showingLogicalTree;

		if(editorUIState->toolbarUI.groupNamesButton)
			editorUIState->toolbarUI.groupNamesButton->down = !editorUIState->showingLogicalNames;
		if(editorUIState->toolbarUI.logicalNamesButton)
			editorUIState->toolbarUI.logicalNamesButton->down = editorUIState->showingLogicalNames;
	}
}

static void wleUITrackerTreeSetDefaultParent(UIMenuItem *item, TrackerHandle *handle)
{
	if (handle)
		wleSetDefaultParent(handle);
}

static void wleUITrackerTreeRClickNode(UITree *tree, UserData unused)
{
	UITreeNode *selected = ui_TreeGetSelected(tree);
	EditorObject **objs = NULL;
	EditorObject *object;

	if (!selected)
		return;

	object = selected->contents;
	if (!object)
		return;
	
	edObjSelectionGetAll(&objs);
	eaForEach(&objs, editorObjectRef);
	edObjMenuPopupAtCursor(wleMenuRightClick, objs, EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "HideDisabledItems", 0));
	eaDestroyEx(&objs, editorObjectDeref);
}

/******
* This function is invoked whenever the selection changes; it should already be invoked by the
* selection harness, so there should be no need to invoke this elsewhere.  This function is used
* to update the selection combo box for selecting an editing target.
* PARAMS:
*   edObjList - EditorObject EArray containing the currently selected objects
******/
void wleUIRefreshEditingSelector(EditorObject **edObjList)
{
	EditorObject *selected = wleAEGetSelected();
	EditorObject *target = NULL;
	int i;
	bool found;

	// clear old contents
	eaClear(&editorUIState->trackerTreeUI.selection);

	//This n! loop is the vast majority of selection time, even with all the reductions to how often it's called. I wish I had a way to optimize it.
	for (i = 0; i < eaSize(&edObjList); i++)
	{
		int j;
		found = false;

		// do not copy over objects that are treated the same for UI purposes
		if (edObjList[i]->type->compareForUIFunc)
		{
			for (j = 0; j < eaSize(&editorUIState->trackerTreeUI.selection); j++)
			{
				if (edObjCompareForUI(edObjList[i], editorUIState->trackerTreeUI.selection[j]) == 0)
				{
					found = true;
					break;
				}
			}
		}
		if (!found && wleAEGetPanelsForType(edObjList[i]->type, NULL))
			eaPush(&editorUIState->trackerTreeUI.selection, edObjList[i]);
	}

	if (selected)
	{
		for (i = 0; i < eaSize(&editorUIState->trackerTreeUI.selection); i++)
		{
			if (edObjCompareForUI(selected, editorUIState->trackerTreeUI.selection[i]) == 0)
			{
				target = editorUIState->trackerTreeUI.selection[i];
				break;
			}
		}

		if (!target)
		{
			if (eaSize(&editorUIState->trackerTreeUI.selection) > 0)
				target = editorUIState->trackerTreeUI.allSelect;
			else
				target = NULL;
		}
	}

	if (eaSize(&editorUIState->trackerTreeUI.selection) > 0)
		eaInsert(&editorUIState->trackerTreeUI.selection, editorUIState->trackerTreeUI.allSelect, 0);

	ui_ComboBoxSetSelectedObject(editorUIState->trackerTreeUI.editing, target);
	wleAESetActiveQueued(target);
	wleUITrackerTreeHighlightEdObj(target);
}

// editing combo box selected
static void wleUIEditingSelected(UIComboBox *combo, void *userdata)
{
	int selected = ui_ComboBoxGetSelected(combo);
	if (selected >= 0)
	{
		EditorObject **edObj = NULL;
		eaPush(&edObj, (*(EditorObject***)combo->model)[selected]);
		editorObjectRef(edObj[0]);
		wleAESetActive(edObj[0]);
		wleUITrackerTreeCenterOnEdObj(edObj[0]);
		wleUIFocusCameraOnEdObjs(edObj);
		eaDestroyEx(&edObj, editorObjectDeref);
	}
	else
		wleAESetActive(NULL);
}

static void wleUIAttribViewAddComboChanged(UIComboBox *combo, UserData unused)
{
	int idx = ui_ComboBoxGetSelected(combo);

	if(idx >= 0 && idx < eaSize(&wleAEGlobalState.validPanels)) {
		WleAEPanel *panel = wleAEGlobalState.validPanels[idx];

		if(panel->addProps) {
			panel->addProps(NULL, NULL);
		} else {
			assert(panel->prop_data.pchPath && panel->prop_data.pTable);
			wleAEAddPropsToSelection(NULL, &panel->prop_data);
		}
	}
	
	ui_ComboBoxSetSelected(combo, -1);
}

static void wleUIAttribStatusDisplay(struct UIList *uiList, struct UIListColumn *col, UI_MY_ARGS, F32 z, CBox *box, int index, void *data)
{
	WleAEPanel *panel = (*uiList->peaModel)[index];
	AtlasTex *tex = NULL;

	if (!panel)
		return;

	if(panel->currentPanelMode == WLE_UI_PANEL_MODE_PINNED)
		tex = atlasLoadTexture("eui_icon_thumbtack");
	else if (panel->currentPanelMode == WLE_UI_PANEL_MODE_FORCE_SHOWN)
		tex = atlasLoadTexture("eui_icon_thumbtack_glowing");

	if (tex) {
		F32 width = ((20.0f - scale*2.0f) / MAX(tex->width, 0.1f));
		F32 height = ((20.0f - scale*2.0f) / MAX(tex->height, 0.1f));
		display_sprite(tex, x+scale, y+scale, z, scale * width, scale * height, 0xFFFFFFFF);
	}
}

static void wleUIAttribPanelDelete(UserData unsused, WleAEPanel *panel)
{
	if(panel->removeProps) {
		panel->removeProps(NULL, NULL);
	} else {
		assert(panel->prop_data.pchPath && panel->prop_data.pTable);
		wleAERemovePropsToSelection(NULL, &panel->prop_data);
	}
}

static void wleUIAttribPanelUnselectAll()
{
	int j;
	for ( j=0; j < eaSize(&wleAEGlobalState.panels); j++ ) {
		WleAEPanel *panel = wleAEGlobalState.panels[j];
		EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, "AttribPanelSelected", panel->name, 0);
	}
}

static void wleUIAttribPanelSetMode(WleAEPanel *panel_clicked, WleUIPanelMode currentPanelMode)
{
	if(EditorPrefGetInt(WLE_PREF_EDITOR_NAME, "AttribPanelSelected", panel_clicked->name, 0) == 0) {
		wleUIAttribPanelUnselectAll();
		panel_clicked->currentPanelMode = currentPanelMode;
		EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, "AttribPanelMode", panel_clicked->name, currentPanelMode);
		EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, "AttribPanelSelected", panel_clicked->name, 1);
	} else {
		int i;
		for ( i=0; i < eaSize(&wleAEGlobalState.ownedPanels); i++ ) {
			WleAEPanel *panel = wleAEGlobalState.ownedPanels[i];
			if(EditorPrefGetInt(WLE_PREF_EDITOR_NAME, "AttribPanelSelected", panel->name, 0) == 0)
				continue;
			panel->currentPanelMode = currentPanelMode;
			EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, "AttribPanelMode", panel->name, currentPanelMode);
		}
	}
	wleAERefresh();
}

static void wleUIAttribPanelSetNormal(UserData unsused, WleAEPanel *panel) { wleUIAttribPanelSetMode(panel, WLE_UI_PANEL_MODE_NORMAL);}
static void wleUIAttribPanelSetPinned(UserData unsused, WleAEPanel *panel) { wleUIAttribPanelSetMode(panel, WLE_UI_PANEL_MODE_PINNED);}
static void wleUIAttribPanelSetShown(UserData unsused, WleAEPanel *panel) { wleUIAttribPanelSetMode(panel, WLE_UI_PANEL_MODE_FORCE_SHOWN);}

static void wleUIAttribViewListMenuPopup(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData unsued)
{
	UIMenu *pSubMenu;
	WleAEPanel *panel;
	bool can_delete;

	if(iRow < 0 || iRow >= eaSize(&wleAEGlobalState.ownedPanels))
		return;

	panel = wleAEGlobalState.ownedPanels[iRow];
	can_delete = (panel->removeProps || (panel->prop_data.pchPath && panel->prop_data.pTable));

	if (editorUIState->attribViewUI.context_menu)
		ui_WidgetQueueFree(UI_WIDGET(editorUIState->attribViewUI.context_menu));

	pSubMenu = ui_MenuCreate("Panel Mode");
	ui_MenuAppendItems(pSubMenu,
		ui_MenuItemCreate("Normal", UIMenuCallback, wleUIAttribPanelSetNormal, panel, NULL),
		ui_MenuItemCreate("Pinned", UIMenuCallback, wleUIAttribPanelSetPinned, panel, NULL),
		ui_MenuItemCreate("Always Shown", UIMenuCallback, wleUIAttribPanelSetShown, panel, NULL),
		NULL);

	editorUIState->attribViewUI.context_menu = ui_MenuCreate("");
	ui_MenuAppendItems(editorUIState->attribViewUI.context_menu,
		ui_MenuItemCreate("Panel Mode", UIMenuSubmenu, NULL, NULL, pSubMenu),
		NULL);

	if(can_delete) {
		ui_MenuAppendItems(editorUIState->attribViewUI.context_menu,
			ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
			ui_MenuItemCreate("Delete", UIMenuCallback, wleUIAttribPanelDelete, panel, NULL),
			NULL);
	}

	editorUIState->attribViewUI.context_menu->widget.scale = emGetSidebarScale() / g_ui_State.scale;
	ui_MenuPopupAtCursor(editorUIState->attribViewUI.context_menu);
}

static void wleUIAttribViewSelectionChanged(UIList *list, UserData unused)
{
	const int * const *peaiRows = ui_ListGetSelectedRows(list);
	int j;

	wleUIAttribPanelUnselectAll();

	for (j = 0; j < eaiSize(peaiRows); j++)
	{	
		int i = eaiGet(peaiRows, j);
		WleAEPanel *panel = wleAEGlobalState.ownedPanels[i];
		EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, "AttribPanelSelected", panel->name, 1);
	}

	wleAERefresh();
}

void wleUIAttribViewPanelRefresh(void)
{
	int j;
	int *peaiNewSelection = NULL;

	for ( j=0; j < eaSize(&wleAEGlobalState.ownedPanels); j++ ) {
		WleAEPanel *panel = wleAEGlobalState.ownedPanels[j];
		bool selected = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, "AttribPanelSelected", panel->name, 0);
		if(selected)
			eaiPush(&peaiNewSelection, j);
	}
	ui_ListSetSelectedRows(editorUIState->attribViewUI.attribList, &peaiNewSelection);
	eaiDestroy(&peaiNewSelection);
}

static void wleUIAttribViewPanelCreate(EMEditorDoc *doc)
{
	int y = 5;
	UIList *list;
	UIListColumn *column;
	UIComboBox *combo;
	UILabel *label;
	EMPanel *panel = emPanelCreate("Selection", "Attributes", 500);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, true);
	editorUIState->attribViewUI.attribViewPanel = panel;

	label = ui_LabelCreate("Editing", 5, y);
	emPanelAddChild(panel, label, false);
	combo = ui_ComboBoxCreate(0, label->widget.y, 1, parse_EditorObject, &editorUIState->trackerTreeUI.selection, "name");
	combo->widget.widthUnit = UIUnitPercentage;
	combo->widget.leftPad = elUINextX(label) + 10;
	ui_ComboBoxSetSelectedCallback(combo, wleUIEditingSelected, NULL);
	emPanelAddChild(panel, combo, true);
	editorUIState->trackerTreeUI.editing = combo;
	y = elUINextY(combo) + 5;

	combo = ui_ComboBoxCreate(5, y, 1, parse_WleAEPanel, &wleAEGlobalState.validPanels, "name");
	ui_ComboBoxSetDefaultDisplayString(combo, "Add Attributes...");
	ui_ComboBoxSetSelectedCallback(combo, wleUIAttribViewAddComboChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(combo), 1, UIUnitPercentage);
	emPanelAddChild(panel, combo, true);
	editorUIState->attribViewUI.attribSelecter = combo;
	y = elUINextY(combo) + 5;

	list = ui_ListCreate(parse_WleAEPanel, &wleAEGlobalState.ownedPanels, 18);
	ui_WidgetSetPosition(UI_WIDGET(list), 5, y);
	ui_ListSetSelectedCallback(list, wleUIAttribViewSelectionChanged, NULL);
	ui_ListSetCellContextCallback(list, wleUIAttribViewListMenuPopup, NULL);
	ui_ListSetMultiselect(list, true);
	emPanelAddChild(panel, list, true);
	column = ui_ListColumnCreateCallback("", wleUIAttribStatusDisplay, NULL);
	column->fWidth = 20;
	ui_ListAppendColumn(list, column);
	column = ui_ListColumnCreateParseName("Attribute", "name", NULL);
	column->fWidth = 200;
	ui_ListAppendColumn(list, column);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1, 1, UIUnitPercentage, UIUnitPercentage);
	editorUIState->attribViewUI.attribList = list;
	y = elUINextY(list) + 5;

	emPanelSetHeight(panel, elUINextY(editorUIState->attribViewUI.attribSelecter) + 120);
	ui_WidgetSetDimensionsEx(UI_WIDGET(emPanelGetPane(panel)), 1, 1, UIUnitPercentage, UIUnitPercentage);	
	wleUIAttribViewPanelRefresh();
}


/******
* This is called during initialization to create the tracker tree panel and all contained widgets.
* PARAMS:
*   doc - EMEditorDoc for which the tracker tree UI is being created
******/
static void wleUITrackerTreeCreate(EMEditorDoc *doc)
{
	EMPanel *panel;
	UITree *tree;
	UIButton *button;
	UILabel *label;
	UISeparator *separator;

	editorUIState->trackerTreeUI.allSelect = editorObjectCreate(NULL, "ENTIRE SELECTION", NULL, EDTYPE_DUMMY);
	editorObjectRef(editorUIState->trackerTreeUI.allSelect);

	editorUIState->trackerTreeUI.trackerTreeSkin = ui_SkinCreate(NULL);
	ui_SkinSetBackgroundEx(editorUIState->trackerTreeUI.trackerTreeSkin, editorUIState->trackerTreeUI.trackerTreeSkin->background[0], colorFromRGBA(0x0000FF33));
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_NormalLocked", editorUIState->trackerTreeUI.fontNormalLocked);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_NormalUnlocked", editorUIState->trackerTreeUI.fontNormalUnlocked);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_LibraryLocked", editorUIState->trackerTreeUI.fontLibraryLocked);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_LibraryUnlocked", editorUIState->trackerTreeUI.fontLibraryUnlocked);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_PrivateLocked", editorUIState->trackerTreeUI.fontPrivateLocked);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_PrivateUnlocked", editorUIState->trackerTreeUI.fontPrivateUnlocked);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_Frozen", editorUIState->trackerTreeUI.fontFrozen);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_Selected", editorUIState->trackerTreeUI.fontSelected);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_UniqueName", editorUIState->trackerTreeUI.fontUniqueName);

	panel = emPanelCreate("Selection", "Tracker Tree", 500);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, true);
	editorUIState->trackerTreeUI.trackerTreePanel = panel;

	tree = ui_TreeCreate(0, 0, 1, 1);
	ui_WidgetSkin(UI_WIDGET(tree), editorUIState->trackerTreeUI.trackerTreeSkin);
	tree->activatedF = wleUITrackerNodeActivate;
	tree->selectedF = wleUITrackerNodeSelect;
	tree->widget.offsetFrom = UITopLeft;
	ui_WidgetSetDimensionsEx(UI_WIDGET(tree), 1, 1, UIUnitPercentage, UIUnitPercentage);
	tree->root.fillF = wleUITrackerNodeFillRoot;
	emPanelAddChild(panel, tree, false);
	editorUIState->trackerTreeUI.trackerTree = tree;
	ui_TreeSetContextCallback(tree, wleUITrackerTreeRClickNode, NULL);
	ui_TreeEnableDragAndDrop(tree);
	tree->dragToNewParent = true;
	ui_TreeSetDragCallback(tree, wleUITrackerTreeDrag, NULL);

	button = ui_ButtonCreate("Collapse all", 0, 0, wleUITrackerTreeCollapseAllClicked, NULL);
	button->widget.width = 0.49;
	button->widget.widthUnit = UIUnitPercentage;
	emPanelAddChild(panel, button, false);

	button = ui_ButtonCreate("Show Scratch", 0, 0, wleUIToolbarToggleEditingScratchLayerCB, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 0, 0, 0, 0, UITopRight);
	button->widget.width = 0.49;
	button->widget.widthUnit = UIUnitPercentage;
	editorUIState->toolbarUI.scratchLayerToggleButton = button;
	emPanelAddChild(panel, button, false);

	label = ui_LabelCreate("Tree:", 0, elUINextY(editorUIState->toolbarUI.scratchLayerToggleButton) + 5);
	emPanelAddChild(panel, label, false);

	button = ui_ButtonCreate("Group", elUINextX(label) + 5, elUINextY(editorUIState->toolbarUI.scratchLayerToggleButton) + 5, wleUIToolbarGroupTreeCB, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(button), "Displays group tree.");
	button->widget.width = 50;
	button->widget.widthUnit = UIUnitFixed;
	editorUIState->toolbarUI.groupTreeButton = button;
	emPanelAddChild(panel, button, false);

	button = ui_ButtonCreate("Logical", elUINextX(button) + 5, elUINextY(editorUIState->toolbarUI.scratchLayerToggleButton) + 5, wleUIToolbarLogicalTreeCB, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(button), "Displays logical tree.");
	button->widget.width = 50;
	button->widget.widthUnit = UIUnitFixed;
	editorUIState->toolbarUI.logicalTreeButton = button;
	emPanelAddChild(panel, button, false);

	separator = ui_SeparatorCreate(UIVertical);
	ui_WidgetSetPosition(UI_WIDGET(separator), elUINextX(button) + 5, elUINextY(editorUIState->toolbarUI.scratchLayerToggleButton) + 5);
	ui_WidgetSetDimensionsEx(UI_WIDGET(separator), UI_WIDGET(separator)->width, button->widget.height, UIUnitFixed, UIUnitFixed);
	emPanelAddChild(panel, separator, false);

	label = ui_LabelCreate("Names:", elUINextX(separator) + 5, elUINextY(editorUIState->toolbarUI.scratchLayerToggleButton) + 5);
	emPanelAddChild(panel, label, false);

	button = ui_ButtonCreate("Group", elUINextX(label) + 5, elUINextY(editorUIState->toolbarUI.scratchLayerToggleButton) + 5, wleUIToolbarGroupNamesCB, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(button), "Displays group names. Double-click edits group name.");
	button->widget.width = 50;
	button->widget.widthUnit = UIUnitFixed;
	editorUIState->toolbarUI.groupNamesButton = button;
	emPanelAddChild(panel, button, false);

	button = ui_ButtonCreate("Logical", elUINextX(button) + 5, elUINextY(editorUIState->toolbarUI.scratchLayerToggleButton) + 5, wleUIToolbarLogicalNamesCB, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(button), "Displays logical names, relative to the currently active scope. Double-click edits this logical name");
	button->widget.width = 50;
	button->widget.widthUnit = UIUnitFixed;
	editorUIState->toolbarUI.logicalNamesButton = button;
	emPanelAddChild(panel, button, false);

	tree->widget.y = elUINextY(button) + 5;
	wleUITrackerTreeRefresh(NULL);
	emPanelSetHeight(panel, elUINextY(button) + 455);
	ui_WidgetSetDimensionsEx(UI_WIDGET(emPanelGetPane(panel)), 1, 1, UIUnitPercentage, UIUnitPercentage);
}

/********************
* TRACKER SEARCH
********************/
static bool wleUITrackerTreeSearchIsUnique(EditorObject *obj)
{
	EditorObject *found = NULL;

	// TODO: determine whether the name is sufficient in distinguishing between "unique" objects
	if (!editorUIState->trackerSearchUI.uniqueObjs)
		editorUIState->trackerSearchUI.uniqueObjs = stashTableCreateWithStringKeys(16, StashDefault);
	if (!stashFindPointer(editorUIState->trackerSearchUI.uniqueObjs, obj->name, &found))
	{
		editorObjectRef(obj);
		stashAddPointer(editorUIState->trackerSearchUI.uniqueObjs, obj->name, obj, false);
		return true;
	}
	else
		return (edObjCompare(obj, found) == 0);
}

static void wleUITrackerTreeSearch(const char *searchText, bool prev, bool select, bool findAll, bool unique)
{
	UITreeIterator *iter;
	WleFilter *activeFilter = ui_ComboBoxGetSelectedObject(editorUIState->trackerSearchUI.searchFilterCombo);
	UITreeRefreshNode *treeState = ui_TreeRefreshNodeCreate(&editorUIState->trackerTreeUI.trackerTree->root);
	UITreeNode *firstNode, *nextNode = NULL;
	EditorObject **objs = NULL;
	static bool lastDir = false;

	if (!editorUIState->trackerSearchUI.searchIterator)
		editorUIState->trackerSearchUI.searchIterator = ui_TreeIteratorCreate(editorUIState->trackerTreeUI.trackerTree, true, true);

	// this is to make sure that consecutive searches that aren't of the same type clear the uniqueness stash
	if (lastDir != prev || !unique || findAll)
		wleUISearchClearUniqueness();
	lastDir = prev;
	
	iter = findAll ? ui_TreeIteratorCreate(editorUIState->trackerTreeUI.trackerTree, true, true) : editorUIState->trackerSearchUI.searchIterator;
	iter->expandNodes = (activeFilter ? activeFilter->ignoreNodeState : true);
	firstNode = ui_TreeIteratorCurr(iter);

	do 
	{
		EditorObject *obj;
		const char *objText;

		nextNode = prev ? ui_TreeIteratorPrev(iter) : ui_TreeIteratorNext(iter);
		obj = nextNode ? nextNode->contents : NULL;
		objText = wleUIEdObjGetDisplayText(obj);
		if (obj && objText && (!activeFilter || wleFilterApply(obj, activeFilter)))
		{
			if (isWildcardMatch(searchText, objText, false, false)) {
				// note: make sure the uniqueness check function is last in this condition, as it actually adds the def to the stash
				if (!unique || wleUITrackerTreeSearchIsUnique(obj))
				{
					editorObjectRef(obj);
					eaPush(&objs, obj);
					if (!findAll)
						break;
				}
			}
		}
	}
	while (nextNode != firstNode);

	// reset tree to old node state
	ui_TreeRefreshEx(editorUIState->trackerTreeUI.trackerTree, treeState);
	if (findAll)
		ui_TreeIteratorFree(iter);

	// deal with found objects
	if (eaSize(&objs) == 0)
		emStatusPrintf("No matching objects were found.");
	else
	{
		int i;
		EditUndoBeginGroup(edObjGetUndoStack());
		if (select)
		{
			edObjSelectList(objs, false, true);
			wleUITrackerTreeCenterOnEdObj(objs[eaSize(&objs)-1]);
		}
		else
		{
			for (i = 0; i < eaSize(&objs); i++)
			{
				wleUITrackerTreeExpandToEdObj(objs[i]);
				wleUITrackerTreeCenterOnEdObj(objs[i]);
			}
		}
		EditUndoEndGroup(edObjGetUndoStack());
	}

	eaDestroyEx(&objs, editorObjectDeref);
}

static void wleUISearchFilterParamsChanged(UIWidget *widget, UserData unused)
{
	wleUISearchClearUniqueness();
}

static void wleUISearchFilterSelected(UIComboBox *combo, UserData unused)
{
	wleUISearchFilterParamsChanged(NULL, NULL);
	wleUIFilterSelected(combo, NULL);
}

static void wleUISearchFilterClearClicked(UIButton *button, UserData unused)
{
	ui_ComboBoxSetSelectedObjectAndCallback(editorUIState->trackerSearchUI.searchFilterCombo, NULL);
}

static void wleUISearchSelectCheckToggled(UICheckButton *check, UserData unused)
{
	bool state = ui_CheckButtonGetState(check);
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SearchSelect", state);
}

static void wleUISearchUniqueCheckToggled(UICheckButton *check, UserData unused)
{
	bool state = ui_CheckButtonGetState(check);
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SearchUnique", state);
}

static void wleUISearchFindNextClicked(UIButton *button, UITextEntry *entry)
{
	const char *searchText = ui_TextEntryGetText(entry);
	wleUITrackerTreeSearch(searchText, false, ui_CheckButtonGetState(editorUIState->trackerSearchUI.selectCheck), false, ui_CheckButtonGetState(editorUIState->trackerSearchUI.uniqueCheck));
}

static void wleUISearchFindPrevClicked(UIButton *button, UITextEntry *entry)
{
	const char *searchText = ui_TextEntryGetText(entry);
	wleUITrackerTreeSearch(searchText, true, ui_CheckButtonGetState(editorUIState->trackerSearchUI.selectCheck), false, ui_CheckButtonGetState(editorUIState->trackerSearchUI.uniqueCheck));
}

static void wleUISearchSelectAllClicked(UIButton *button, UITextEntry *entry)
{
	const char *searchText = ui_TextEntryGetText(entry);
	wleUITrackerTreeSearch(searchText, false, true, true, ui_CheckButtonGetState(editorUIState->trackerSearchUI.uniqueCheck));
}

void wleUISearchClearUniqueness(void)
{
	stashTableClearEx(editorUIState->trackerSearchUI.uniqueObjs, NULL, editorObjectDeref);
}

/********************
* OBJECT LIBRARY
********************/
/******
* This function calculates the width of any of the object-library-related trees, returning the
* width so it can be set on the tree.
* PARAMS:
*   node - UITreeNode from which to start calculating the width; the function recurses through all
*          of node's children
*   offset - F32 amount to add to the returned width; represents the left-identation padding
* RETURNS:
*   F32 maximum width of the text for the specified node and its children
******/
static F32 wleUIObjectTreeGetWidth(UITreeNode *node, F32 offset)
{
	AtlasTex *opened = (g_ui_Tex.minus);
	char message[1024] = {0};
	F32 maxWidth;
	int i;

	if (node->table)
	{
		char token[256];
		FORALL_PARSETABLE(node->table, i)
		{
			if (node->table[i].name && 0 == strcmpi(node->table[i].name, "name"))
			{
				if (TokenToSimpleString(node->table, i, node->contents, token, 256, 0))
					sprintf(message, "%s", token);
				else
					sprintf(message, "NULL");
			}
		}
	}

	if (!message[0])
		sprintf(message, "NULL");

	maxWidth = offset + gfxfont_StringWidthf(&g_font_Sans, 1, 1, "%s", message);
	for (i = 0; i < eaSize(&node->children); i++)
		maxWidth = MAX(maxWidth, wleUIObjectTreeGetWidth(node->children[i], offset + UI_STEP + opened->width));

	return maxWidth;
}

static void wleUIObjectNodeDisplay(UITreeNode *node, void *unused, UI_MY_ARGS, F32 z)
{
	char message[1024];

	if (node->table)
	{
		int i;
		char token[256];
		FORALL_PARSETABLE(node->table, i)
		{
			if (node->table[i].name && 
				(0==strcmpi(node->table[i].name, "name") ||
				0==strcmpi(node->table[i].name, "DisplayName") ||
				0==strcmpi(node->table[i].name, "resourceName")))
			{
				if (TokenToSimpleString(node->table, i, node->contents, token, 256, 0))
					sprintf(message, "%s", token);
				else
					sprintf(message, "NULL");
				ui_TreeDisplayText(node, message, UI_MY_VALUES, z);
				return;
			}
		}
	}

	sprintf(message, "NULL");
	ui_TreeDisplayText(node, message, UI_MY_VALUES, z);
}

static void wleUIObjectNodeFill(UITreeNode *parent_node, void *unused)
{
	int i, j;

	FORALL_PARSETABLE(parent_node->table, i)
	{
		if (TOK_GET_TYPE(parent_node->table[i].type) == TOK_STRUCT_X && parent_node->table[i].type & TOK_EARRAY)
		{
			void **children = *TokenStoreGetEArray(parent_node->table, i, parent_node->contents, NULL);
			for (j = 0; j < eaSize(&children); ++j)
			{
				void *child_data = children[j];
				ParseTable *child_table = parent_node->table[i].subtable;
				UITreeNode *newNode;
				bool is_objlib_leaf = child_table == parse_ResourceInfo;
				bool is_objlib_folder = child_table == parse_ResourceGroup;
				bool hidden = false;
				const char *name = NULL;

				if (is_objlib_leaf)
				{
					ResourceInfo *entry = child_data;
					GroupDef *def = objectLibraryGetGroupDefFromResource(entry, false);
					if (!def || groupIsPrivate(def))
						continue;
					if (def->name_str[0] == '_')
						hidden = true;
					child_table = parse_GroupDef;
					child_data = def;
					name = entry->resourceName;
				}
				else if (is_objlib_folder)
				{
					ResourceGroup *folder = child_data;
					name = folder->pchName;
				}

				if (!hidden || editorUIState->showHiddenLibs)
				{
					newNode = ui_TreeNodeCreate(
						parent_node->tree, name?cryptAdler32String(name):0, child_table, child_data,
						is_objlib_leaf ? NULL : wleUIObjectNodeFill, NULL,
						wleUIObjectNodeDisplay, NULL, 13);
					ui_TreeNodeAddChild(parent_node, newNode);
				}
			}

			if (j)
			{
				parent_node->tree->width = 0;
				parent_node->tree->width = wleUIObjectTreeGetWidth(&parent_node->tree->root, 10);
			}
		}
	}
}

static void wleUIObjectNodeSelect(UITree *tree, void *unused)
{
	if (tree->selected && tree->selected->table == parse_GroupDef && !mouseIsDown(MS_RIGHT))
	{
		GroupDef *def = ui_TreeGetSelected(tree)->contents;
		wleObjectPlace(def->name_uid);
	}
}

static void wleUIObjectNodeActivate(UITree *tree, void *unused)
{
	if (!tree->selected || !tree->selected->fillF)
		return;

	if (tree->selected && tree->selected->open)
		ui_TreeNodeCollapse(tree->selected);
	else
		ui_TreeNodeExpand(tree->selected);
}

static void wleUIObjectNodeRClickLock(UIMenuItem *item, ResourceInfo *ole)
{
	GroupDef *def = objectLibraryGetGroupDefByName(ole->resourceName, false);
	objectLibraryGroupSetEditable(def);
}

static void wleUIObjectNodeRClickPreview(UIMenuItem *item, ResourceInfo *ole)
{
	PreviewResource(ole->resourceDict, ole->resourceName);
}

static void wleUIObjectNodeRClickDelete(UIMenuItem *item, ResourceInfo *ole)
{
	wleDeleteFromLib(ole);
}

static void wleUIObjectNodeRClick(UITree *tree, void *unused)
{
	UITreeNode *selected = ui_TreeGetSelected(tree);

	if (editorUIState->objectTreeUI.objectTreeRClickMenu)
		ui_MenuClear(editorUIState->objectTreeUI.objectTreeRClickMenu);
	else
		editorUIState->objectTreeUI.objectTreeRClickMenu = ui_MenuCreate("Object Library Right-Click Menu");

	editorUIState->objectTreeUI.objectTreeRClickMenu->widget.scale = emGetSidebarScale() / g_ui_State.scale;
	if (selected && selected->table == parse_ResourceInfo)
	{
		ResourceInfo *ole = selected->contents;
		char itemName[MAX_PATH + 10];
		UIMenuItem *item;
		GroupDef *def = objectLibraryGetGroupDefByName(ole->resourceName, false);
		
		editorUIState->objectTreeUI.objectTreeRClickMenu->itemWidth = 0;
		
		sprintf(itemName, "Lock \"%s\"", ole->resourceLocation);
		item = ui_MenuItemCreate(itemName, UIMenuCallback, wleUIObjectNodeRClickLock, ole, NULL);
		item->active = def && !groupIsEditable(def);
		ui_MenuAppendItem(editorUIState->objectTreeUI.objectTreeRClickMenu, item);

		sprintf(itemName, "Preview \"%s\"", ole->resourceName);
		item = ui_MenuItemCreate(itemName, UIMenuCallback, wleUIObjectNodeRClickPreview, ole, NULL);
		ui_MenuAppendItem(editorUIState->objectTreeUI.objectTreeRClickMenu, item);

		/* TODO: uncomment this when we've resolved how to deal with deletion from library in new locking system
		item = ui_MenuItemCreate("Delete from library", UIMenuCallback, objectTreeDelete, ole, NULL);
		item->active = wleFileLocked(ole->filename, false, false);
		ui_MenuAppendItem(editorUIState->objectTreeUI.objectTreeRClickMenu, item);*/
		ui_MenuPopupAtCursor(editorUIState->objectTreeUI.objectTreeRClickMenu);
	}		
}

static void wleUIObjectLibraryCollapseAllClicked(UIButton *button, UserData unused)
{
	ui_TreeNodeExpand(&editorUIState->objectTreeUI.objectTree->root);
}

static bool wleUIObjectLibrarySearchSelected(EMPicker *picker, EMPickerSelection **selections, void *data)
{
	ResourceInfo *entry;
	GroupDef *def = NULL;

	if (eaSize(&selections) == 0)
		return false;

	assert(selections[0]->table == parse_ResourceInfo);
	entry = selections[0]->data;
	wleObjectPlace(entry->resourceID);

	// open object library to entry
// 	def = objectLibraryGetGroupDefFromResource(entry, false);
// 	if (def)
// 		elUITreeExpandToNode(&editorUIState->objectTreeUI.objectTree->root, def);

	return true;
}

static void wleUIObjectLibrarySearchClicked(UIButton *button, UserData unused)
{
	emPickerShow(wleGetObjectPicker(), "Place", false, wleUIObjectLibrarySearchSelected, NULL);
}

static void wleUIObjectTreeShowHiddenToggle(UICheckButton *check, UserData unused)
{
	if (editorUIState->objectTreeUI.objectTree)
		ui_TreeRefresh(editorUIState->objectTreeUI.objectTree);
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ShowHiddenLibs", editorUIState->showHiddenLibs);
}

static void wleUIObjectTreeReplaceOnCreateToggle(UICheckButton *check, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ReplaceOnCreate", editState.replaceOnCreate);
}

static void wleUIObjectTreeRepeatCreateToggle(UICheckButton *check, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "RepeatCreate", editState.repeatCreateAcrossSelection);
}

/******
* This function refreshes the contents of the object library.
******/
void wleUIObjectTreeRefresh(void)
{
	ResourceGroup *root = objectLibraryRefreshRoot();
	if (editorUIState && editorUIState->objectTreeUI.objectTree)
	{
		editorUIState->objectTreeUI.objectTree->root.fillData = root;
		ui_TreeRefresh(editorUIState->objectTreeUI.objectTree);
	}
	emObjectPickerRefresh();
}


/********************
* RENAME TRACKER DIALOG
********************/
typedef struct WleUIRenameTrackerWin
{
	UIWindow *win;
	UITextEntry *entry;
	UIButton *okButton;

	// populated according to the rename mode
	TrackerHandle *scopeHandle;
	char *oldScopeName;
	bool settingLogicalName;
	TrackerHandle *handle;
} WleUIRenameTrackerWin;

static void wleUIEdObjRenameChanged(UITextEntry *entry, WleUIRenameTrackerWin *renameUI)
{
	const char *name = ui_TextEntryGetText(entry);
	ui_SetActive(UI_WIDGET(renameUI->okButton), (name && name[0]));
}

static void wleUIEdObjRenameEntryEnter(UITextEntry *entry, WleUIRenameTrackerWin *renameUI)
{
	ui_ButtonClick(renameUI->okButton);
}

static bool wleUIEdObjRenameCancel(UIWidget *widget, WleUIRenameTrackerWin *renameUI)
{
	elUIWindowClose(NULL, renameUI->win);
	trackerHandleDestroy(renameUI->scopeHandle);
	trackerHandleDestroy(renameUI->handle);
	SAFE_FREE(renameUI->oldScopeName);
	free(renameUI);
	return true;
}

static void wleUIEdObjRenameOk(UIButton *okButton, WleUIRenameTrackerWin *renameUI)
{
	if (renameUI->oldScopeName)
	{
		if (wleOpSetLogicalGroupUniqueScopeName(renameUI->scopeHandle, renameUI->oldScopeName, ui_TextEntryGetText(renameUI->entry)))
			wleUIEdObjRenameCancel(NULL, renameUI);
	}
	else if (renameUI->settingLogicalName)
	{
		if (wleOpSetUniqueScopeName(renameUI->scopeHandle, renameUI->handle, ui_TextEntryGetText(renameUI->entry)))
			wleUIEdObjRenameCancel(NULL, renameUI);
	}
	else
	{
		wleOpRename(renameUI->handle, ui_TextEntryGetText(renameUI->entry));
		wleUIEdObjRenameCancel(NULL, renameUI);
	}
}

void wleUIEdObjRenameDialog(EditorObject *edObj)
{
	WleUIRenameTrackerWin *renameUI = NULL;
	const char *entryText = NULL;
	char *windowTitle = NULL;

	UIWindow *win;
	UILabel *label;
	UITextEntry *entry;
	UIButton *button;

	editorObjectRef(edObj);
	if (edObj->type->objType == EDTYPE_LOGICAL_GROUP)
	{
		WorldZoneMapScope *zmapScope = zmapGetScope(NULL);
		const char *zmapScopeName = edObj->obj;
		WorldLogicalGroup *logicalGroup = zmapScope ? (WorldLogicalGroup*) worldScopeGetObject((WorldScope*) zmapScope, zmapScopeName) : NULL;

		GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
		WorldScope *scope = scopeTracker ? scopeTracker->closest_scope : (WorldScope*) zmapScope;
		const char *activeScopeName = worldScopeGetObjectName(scope, (WorldEncounterObject*) logicalGroup);

		TrackerHandle *tempHandle;
		const char *tempName;
		GroupTracker *parent;
		bool valid = true;

		if (!activeScopeName)
		{
			editorObjectDeref(edObj);
			return;
		}

		// validate editability
		if (!scopeTracker)
			scopeTracker = layerGetTracker(logicalGroup->common_data.layer);
		if (!wleTrackerIsEditable(trackerHandleFromTracker(scopeTracker), true, true, false))
			valid = false;

		// validate editability of parent scope tracker
		parent = trackerGetScopeTracker(scopeTracker);
		if (parent && parent->def && !wleTrackerIsEditable(trackerHandleFromTracker(parent), false, true, true))
			valid = false;

		// validate that unique name in "in-between" scopes has been specified
		tempHandle = trackerHandleCreate(logicalGroup->common_data.tracker);
		tempName = worldScopeGetObjectName(logicalGroup->common_data.closest_scope, (WorldEncounterObject*) logicalGroup);
		assert(tempHandle && tempName);
		if (valid && !wleIsUniqueNameable(editorUIState->trackerTreeUI.activeScopeTracker, tempHandle, tempName))
		{
			emStatusPrintf("Logical group \"%s\" first needs to have a permanent name specified at all subscopes.", activeScopeName);
			valid = false;
		}

		if (!valid)
		{
			editorObjectDeref(edObj);
			return;
		}

		renameUI = calloc(1, sizeof(*renameUI));
		renameUI->oldScopeName = strdup(activeScopeName);
		
		entryText = renameUI->oldScopeName;
		windowTitle = "Set Logical Group Name";
	}
	else if (edObj->type->objType == EDTYPE_TRACKER || edObj->type->objType == EDTYPE_LAYER)
	{
		GroupTracker *renamedTracker = (edObj->type->objType == EDTYPE_TRACKER ? trackerFromTrackerHandle(edObj->obj) : layerGetTracker(edObj->obj));
		TrackerHandle *handle = trackerHandleCreate(renamedTracker);
		GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
		bool settingLogicalName = false;
		bool isScratch = (!renamedTracker->parent && renamedTracker->parent_layer && renamedTracker->parent_layer->scratch); //Don't allow the changing of the scratch layer name.
		
		if (!handle || !renamedTracker || isScratch)
		{
			editorObjectDeref(edObj);
			return;
		}

		// determine mode of operation and validate trackers
		if(editorUIState->showingLogicalNames)
		{
			if (!scopeTracker)
				scopeTracker = layerGetTracker(renamedTracker->parent_layer);
			if (scopeTracker && scopeTracker->def)
			{
				if (renamedTracker->def && groupDefNeedsUniqueName(renamedTracker->def))
					settingLogicalName = true;
			}
		}

		if (settingLogicalName)
		{
			TrackerHandle *checkHandle = trackerHandleCreate(scopeTracker);
			GroupTracker *parent;
			bool valid = true;

			// validate editability
			if (!wleTrackerIsEditable(checkHandle, true, true, false))
				valid = false;
			trackerHandleDestroy(checkHandle);

			parent = trackerGetScopeTracker(scopeTracker);
			if (parent && parent->def)
			{
				checkHandle = trackerHandleCreate(parent);
				if (!wleTrackerIsEditable(checkHandle, false, true, true))
					valid = false;
				trackerHandleDestroy(checkHandle);
			}

			// validate that unique name in "in-between" scopes has been specified
			if (valid && !wleIsUniqueNameable(editorUIState->trackerTreeUI.activeScopeTracker, handle, NULL))
			{
				emStatusPrintf("Tracker \"%s\" first needs to have a permanent name specified at all subscopes.", renamedTracker->def->name_str);
				valid = false;
			}

			if (!valid)
			{
				editorObjectDeref(edObj);
				return;
			}
		}
		else
		{
			// normal group rename
			// check file lock
			if (!wleTrackerIsEditable(handle, true, true, false))
			{
				editorObjectDeref(edObj);
				return;
			}

			if (!renamedTracker->def)
			{
				emStatusPrintf("Cannot rename the root tracker or model-based object library pieces!");
				editorObjectDeref(edObj);
				return;
			}
		}

		renameUI = calloc(1, sizeof(*renameUI));
		renameUI->settingLogicalName = settingLogicalName;
		renameUI->handle = trackerHandleCopy(handle);

		if (settingLogicalName)
		{
			windowTitle = "Set Logical Name";
			entryText = wleTrackerHandleToUniqueName(editorUIState->trackerTreeUI.activeScopeTracker, handle);

			// check if it is unnamed by checking the GROUP_UNNAMED_PREFIX 
			{
				size_t unnamedPrefixNameLen = strlen(GROUP_UNNAMED_PREFIX);
				if (strncmp(GROUP_UNNAMED_PREFIX, entryText, unnamedPrefixNameLen) == 0)
				{	// this is the default "UNNAMED_" name, 
					// so strip the UNNAMED_ and then make sure we have a unique name
					static char uniqueName[MAX_PATH] = {0};

					if (GROUP_NAME_CREATED == groupDefScopeCreateUniqueName(scopeTracker->def, "", 
																			entryText + unnamedPrefixNameLen, 
																			uniqueName, MAX_PATH, false))
					{	
						entryText = uniqueName;
					}
				}
			}
		}
		else
		{
			windowTitle = "Set Group Name";
			entryText = renamedTracker->def->name_str;
		}
	}

	if (!renameUI)
	{
		editorObjectDeref(edObj);
		return;
	}

	win = ui_WindowCreate("", 0, 0, 300, 0);
	label = ui_LabelCreate("New name:", 5, 5);
	renameUI->scopeHandle = trackerHandleCopy(editorUIState->trackerTreeUI.activeScopeTracker);

	ui_WindowAddChild(win, label);

	// create the rename window
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	entry->widget.offsetFrom = UITopLeft;
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	entry->widget.leftPad = elUINextX(label) + 5;
	entry->widget.rightPad = 5;
	ui_TextEntrySetSelectOnFocus(entry, true);
	ui_TextEntrySetEnterCallback(entry, wleUIEdObjRenameEntryEnter, renameUI);
	ui_WindowAddChild(win, entry);

	assert(windowTitle);
	ui_WindowSetTitle(win, windowTitle);
	if (entryText)
		ui_TextEntrySetText(entry, entryText);

	renameUI->entry = entry;
	renameUI->win = win;

	ui_WindowSetCloseCallback(win, wleUIEdObjRenameCancel, renameUI);
	button = elUIAddCancelOkButtons(win, wleUIEdObjRenameCancel, renameUI, wleUIEdObjRenameOk, renameUI);
	renameUI->okButton = button;
	ui_TextEntrySetChangedCallback(entry, wleUIEdObjRenameChanged, renameUI);
	wleUIEdObjRenameChanged(entry, renameUI);
	win->widget.height = elUINextY(button) + elUINextY(entry) + 5;
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
	ui_SetFocus(UI_WIDGET(entry));

	editorObjectDeref(edObj);
}

/********************
* MAP LAYERS
********************/
// map layer callbacks
static void wleUIMapLayerToggleVisible(EMMapLayerType *map_layer, ZoneMapLayer *layer, bool visible)
{
	// TomY TODO find a way to implement this
	if (!visible && layerGetMode(layer) != LAYER_MODE_EDITABLE)
		emMapLayerSetVisible(map_layer, true);
	else
	{
		TrackerHandle *handle = trackerHandleFromTracker(layerGetTracker(layer));
		if (!handle)
			return;
		if (visible)
			wleTrackerUnhide(handle);
		else
			wleTrackerHide(handle);
	}
}

static void wleUIMapLayerViewGroupTree(EMMapLayerType *map_layer, ZoneMapLayer *layer)
{
	wleUISetLayerMode(layer, LAYER_MODE_GROUPTREE, false);
}

static void wleUIMapLayerViewTerrain(EMMapLayerType *map_layer, ZoneMapLayer *layer)
{
	wleUISetLayerMode(layer, LAYER_MODE_TERRAIN, false);
}

static void wleUIMapLayerLock(EMMapLayerType *map_layer, ZoneMapLayer *layer)
{
	wleUISetLayerMode(layer, LAYER_MODE_EDITABLE, false);
}

/******
* This function recreates the map layers for the Editor Manager's map layer panel.
******/
EMMapLayerType **wleMapLayers;
void wleUIMapLayersRefresh(void)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < eaSize(&wleMapLayers); ++i)
	{
		emMapLayerRemove(wleMapLayers[i]);
		emMapLayerDestroy(wleMapLayers[i]);
	}
	eaSetSize(&wleMapLayers, 0);

	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		// We don't want the scratch layer to be in the layers list
		if (layer && !layer->scratch)
		{
			const char *filename = layerGetFilename(layer);
			EMMapLayerType *emLayer = emMapLayerCreate(filename, getFileNameConst(filename), "World", layer, wleUIMapLayerToggleVisible, NULL);

			if (filename)
				eaPush(&wleMapLayers, emLayer);

			emMapLayerMenuItemAdd(emLayer, "View Geometry", wleUIMapLayerViewGroupTree);
			emMapLayerMenuItemAdd(emLayer, "View Terrain", wleUIMapLayerViewTerrain);
			emMapLayerMenuItemAdd(emLayer, "Edit", wleUIMapLayerLock);

			emMapLayerAdd(emLayer);
		}
	}

	PERFINFO_AUTO_STOP();
}


/********************
* LOCK MANAGER
********************/
static void wleUILockMngrFileSelectCancel(UIWindow *modalWin)
{
	if (modalWin)
	{
		ui_WindowSetModal(modalWin, true);
		ui_WindowHide(modalWin);
		ui_WindowShow(modalWin);
	}
}

static void wleUILockMngrFileSelectOk(const char *dir, const char *file, UIWindow *modalWin)
{
	char relPath[MAX_PATH];

	sprintf(relPath, "%s/%s", dir, file);
	wleOpLockFile(relPath, NULL, NULL);
	wleUILockMngrFileSelectCancel(modalWin);
}

static void wleUILockMngrAdd(UIButton *button, UIWindow *modalWin)
{
	UIWindow *browser;
	char startDir[MAX_PATH];

	fileLocateWrite("object_library", startDir);
	if (modalWin)
	{
		ui_WindowSetModal(modalWin, false);
		ui_WindowHide(modalWin);
		ui_WindowShow(modalWin);
	}
	browser = ui_FileBrowserCreate("Add/Load Group File...", "Add", UIBrowseNew, UIBrowseFiles, true, startDir, startDir, NULL, "objlib", wleUILockMngrFileSelectCancel, modalWin, wleUILockMngrFileSelectOk, modalWin);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}
}

void wleUILockMngrRefresh(void)
{
	EMEditorDoc *worldDoc = wleGetWorldEditorDoc();
	bool gfileNodeOpen = true;

	if (!editorUIState)
		return;

	if (editorUIState->lockMngrUI.groupFileNode)
		gfileNodeOpen = editorUIState->lockMngrUI.groupFileNode->open;

	ui_TreeRefresh(editorUIState->lockMngrUI.tree);

	// we reopen the group file node in order to refresh the locked object library file EArray
	if (editorUIState->lockMngrUI.groupFileNode)
	{
		ui_TreeNodeExpand(editorUIState->lockMngrUI.groupFileNode);
		if (!gfileNodeOpen)
			ui_TreeNodeCollapse(editorUIState->lockMngrUI.groupFileNode);
	}
}

static void wleUILockMngrZoneMapNodeDisplay(UITreeNode *node, ZoneMap *zmap, UI_MY_ARGS, F32 z)
{
	const char *c = NULL;
	AtlasTex *tex;
	UIStyleFont *font = NULL;

	c = strrchr(zmapGetFilename(zmap), '/') + 1;

	if (!zmapLocked(zmap))
		font = ui_StyleFontGet("WorldEditor_NormalUnlocked");

	tex = atlasLoadTexture(zmapLocked(zmap) ? "eui_gimme_ok" : "eui_gimme_readonly");
	display_sprite(tex, x, y, z, scale, scale, 0xFFFFFFFF);

	ui_TreeDisplayText(node, c, x + 13 * scale, y, w, h, scale, z);
}

static void wleUILockMngrFillZoneMap(UITreeNode *node, UserData unused)
{
	ZoneMap *zmap = worldGetActiveMap();
	UITreeNode *newNode;
	assert(zmap);

	newNode = ui_TreeNodeCreate(node->tree, 1, NULL, zmap, NULL, NULL, wleUILockMngrZoneMapNodeDisplay, zmap, 14);
	ui_TreeNodeAddChild(node, newNode);
}

static void wleUILockMngrLayerNodeDisplay(UITreeNode *node, ZoneMapLayer *layer, UI_MY_ARGS, F32 z)
{
	GroupTracker *tracker = layerGetTracker(layer);
	const char *c = NULL;
	AtlasTex *tex;
	UIStyleFont *font = NULL;

	if (tracker && tracker->def)
		c = tracker->def->name_str;
	else
	{
		const char *layerFile = layerGetFilename(layer);

		c = strrchr(layerFile, '/');
		if (!c)
			c = layerFile;
		else
			c++;
	}

	if (layerGetLocked(layer) == 2)
		font = ui_StyleFontGet("WorldEditor_LibraryUnlocked");
	else if (layerGetLocked(layer) == 0)
		font = ui_StyleFontGet("WorldEditor_NormalUnlocked");

	tex = atlasLoadTexture(layerGetLocked(layer) == 3 ? "eui_gimme_ok" : "eui_gimme_readonly");
	display_sprite(tex, x, y, z, scale, scale, 0xFFFFFFFF);
	
	ui_TreeDisplayText(node, c, x + 13 * scale, y, w, h, scale, z);
}

static void wleUILockMngrFillLayers(UITreeNode *node, UserData unused)
{
	int i;

	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer)
		{
			UITreeNode *newNode;
			const char *layerFilename = layerGetFilename(layer);
			U32 crc;

			if (!layerFilename)
				continue;

			cryptAdler32Init();
			cryptAdler32Update(layerFilename, (int) strlen(layerFilename));
			crc = cryptAdler32Final();

			newNode = ui_TreeNodeCreate(node->tree, crc, NULL, layer, NULL, NULL, wleUILockMngrLayerNodeDisplay, layer, 14);
			ui_TreeNodeAddChild(node, newNode);
		}
	}
}

static void wleUILockMngrGroupFileNodeDisplay(UITreeNode *node, GroupDefLockedFile *file, UI_MY_ARGS, F32 z)
{
	const char *c = NULL;
	AtlasTex *tex;
	UIStyleFont *font = NULL;
	const char *filename = objectLibraryGetFilePath(file);

	c = strrchr(filename, '/');
	if (!c)
		c = filename;
	else
		c++;

	font = ui_StyleFontGet("WorldEditor_NormalUnlocked");

	tex = atlasLoadTexture("eui_gimme_ok");
	display_sprite(tex, x, y, z, scale, scale, 0xFFFFFFFF);

	ui_TreeDisplayText(node, c, x + 13 * scale, y, w, h, scale, z);
}

static void wleUILockMngrFillGroupFiles(UITreeNode *node, UserData unused)
{
	int i;
	GroupDefLockedFile **file_list = NULL;
	eaDestroy(&editorUIState->lockedObjLib);
	objectLibraryGetEditableFiles(&file_list);
	for (i = 0; i < eaSize(&file_list); i++)
	{
		UITreeNode *newNode;
		GroupDefLockedFile *file = file_list[i];
		const char *filename = objectLibraryGetFilePath(file);
		U32 crc;

		if (!file || !filename)
			continue;

		// update locked list
		eaPush(&editorUIState->lockedObjLib, file);

		cryptAdler32Init();
		cryptAdler32Update(filename, (int) strlen(filename));
		crc = cryptAdler32Final();

		newNode = ui_TreeNodeCreate(node->tree, crc, NULL, file, NULL, NULL, wleUILockMngrGroupFileNodeDisplay, file, 14);
		ui_TreeNodeAddChild(node, newNode);
	}
	eaDestroy(&file_list);
}

static void wleUILockMngrTreeActivated(UITree *tree, UserData unused)
{
	UITreeNode *selected = ui_TreeGetSelected(tree);

	if (!selected)
		return;

	// toggle lock state
	if (selected->displayF == wleUILockMngrZoneMapNodeDisplay)
	{
		ZoneMap *zmap = (ZoneMap*) selected->contents;
		if (!zmapInfoLocked(zmapGetInfo(zmap)))
			zmapInfoLock(zmapGetInfo(zmap));
		// TODO: add zmap unlocking with prompt
	}
	else if (selected->displayF == wleUILockMngrLayerNodeDisplay)
	{
		ZoneMapLayer *layer = (ZoneMapLayer*) selected->contents;
		if (layerGetLocked(layer) == 3)
			wleUIUnlockLayerWrapper(layer);
		else if (layerGetLocked(layer) != 2)
			wleUILockLayerWrapper(layer);
	}
	else if (selected->displayF == wleUILockMngrGroupFileNodeDisplay)
	{
 		GroupDefLockedFile *file = (GroupDefLockedFile*) selected->contents;
		wleUILockGroupFileWrapper(file);
	}
}

static void wleUILockMngrTreeRClick(UITree *tree, UserData unused)
{
	UITreeNode *selected = ui_TreeGetSelected(tree);

	if (!selected)
		return;

	if (!editorUIState->lockMngrUI.rclickMenu)
		editorUIState->lockMngrUI.rclickMenu = ui_MenuCreate("");
	eaDestroyEx(&editorUIState->lockMngrUI.rclickMenu->items, ui_MenuItemFree);

	// pop up menu
	if (selected->displayF == wleUILockMngrLayerNodeDisplay)
		wleUILayerMenuItemsAdd(editorUIState->lockMngrUI.rclickMenu, selected->contents);
	else if (selected->displayF == wleUILockMngrGroupFileNodeDisplay)
		wleUIGroupFileMenuItemsAdd(editorUIState->lockMngrUI.rclickMenu, selected->contents);
	editorUIState->lockMngrUI.rclickMenu->widget.scale = emGetSidebarScale() / g_ui_State.scale;
	ui_MenuPopupAtCursor(editorUIState->lockMngrUI.rclickMenu);
}

static void wleUILockMngrFillRoot(UITreeNode *node, UserData unused)
{
	UITreeNode *newNode;

	newNode = ui_TreeNodeCreate(node->tree, 3, NULL, NULL,
		wleUILockMngrFillZoneMap, NULL, ui_TreeDisplayText, "Zone Map", 14);
	ui_TreeNodeAddChild(node, newNode);
	newNode = ui_TreeNodeCreate(node->tree, 1, NULL, NULL,
		wleUILockMngrFillLayers, NULL, ui_TreeDisplayText, "Layers", 14);
	ui_TreeNodeAddChild(node, newNode);
	newNode = ui_TreeNodeCreate(node->tree, 2, NULL, NULL,
		wleUILockMngrFillGroupFiles, NULL, ui_TreeDisplayText, "Object Libraries", 14);
	editorUIState->lockMngrUI.groupFileNode = newNode;
	ui_TreeNodeAddChild(node, newNode);
}


/******
* AUTO-LOCK PROMPT
******/
static char **wleUILockPromptFiles = NULL;
static UIWindow *wleUIAutoLockWin = NULL;
static UIButton *wleUIAutoLockOk = NULL;

static void wleUILockPromptMakeText(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrPrintf(output, "%s", (char*)(*(list->peaModel))[row]);
}

static void wleUILockPromptCancel(UIButton *button, UserData unused)
{
	if (wleUIAutoLockWin)
	{
		eaDestroyEx(&wleUILockPromptFiles, NULL);
		ui_WindowHide(wleUIAutoLockWin);
	}
}

static void wleUILockPromptOK(UIButton *button, UserData unused)
{
	if (wleUIAutoLockWin)
	{
		// attempt to lock the files
		int i, j;
		for (i = 0; i < eaSize(&wleUILockPromptFiles); i++)
		{
			ZoneMapLayer *layer = NULL;
			for (j = zmapGetLayerCount(NULL)-1; j >= 0; --j)
			{
				ZoneMapLayer *zmap_layer = zmapGetLayer(NULL, j);
				if (zmap_layer && !strcmp(layerGetFilename(zmap_layer), wleUILockPromptFiles[i]))
				{
					layer = zmap_layer;
					break;
				}
			}
			if (layer)
			{
				wleUISetLayerMode(layer, LAYER_MODE_EDITABLE, false);
			}
			else
			{
				objectLibrarySetFileEditable(wleUILockPromptFiles[i]);
			}
		}

		wleUILockPromptCancel(NULL, NULL);
	}
}

static void wleUILockPromptCreate(void)
{
	UIWindow *win;
	UILabel *label;
	UIList *list;
	UIListColumn *col;

	PERFINFO_AUTO_START_FUNC();

	win = ui_WindowCreate("File locker", 0, 0, 450, 150);
	label = ui_LabelCreate("Your operation failed because you need locks on the following files:", 5, 5);
	ui_WindowAddChild(win, label);
	list = ui_ListCreate(NULL, &wleUILockPromptFiles, 15);
	ui_WidgetSetDimensionsEx((UIWidget*) list, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx((UIWidget*) list, 5, 5, 25, 40);
	col = ui_ListColumnCreate(UIListTextCallback, "Filename", (intptr_t) wleUILockPromptMakeText, NULL);
	ui_ListAppendColumn(list, col);
	ui_WindowAddChild(win, list);
	label = ui_LabelCreate("Do you wish to lock these files?", 5, 20);
	label->widget.offsetFrom = UIBottomLeft;
	ui_WindowAddChild(win, label);
	wleUIAutoLockOk = elUIAddCancelOkButtons(win, wleUILockPromptCancel, NULL, wleUILockPromptOK, NULL);
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	wleUIAutoLockWin = win;

	PERFINFO_AUTO_STOP();
}

static void wleEventDebugWindowCreate(EMEditorDoc *doc)
{
	extern EventEditor* s_EncounterLayerEventDebugEditor;
	UIWindow *window;
	
	if (!s_EncounterLayerEventDebugEditor)
		s_EncounterLayerEventDebugEditor = eventeditor_Create(NULL, MDEEventLogSendFilterToServer, NULL, false);

	window = eventeditor_GetWindow(s_EncounterLayerEventDebugEditor);
	ui_WindowSetTitle(window, "Event Debug Filter");
	eaPush(&doc->ui_windows, window);
}

void wleUILockPrompt(const char *filename)
{
	int i;

	// if already in list, dont add again.
	for (i = 0; i < eaSize(&wleUILockPromptFiles); i++)
		if (strcmp(wleUILockPromptFiles[i],filename) == 0)
			return;

	eaPush(&wleUILockPromptFiles, strdup(filename));
	if (wleUIAutoLockWin && !ui_WindowIsVisible(wleUIAutoLockWin) && eaSize(&wleUILockPromptFiles) > 0)
	{
		ui_WindowShow(wleUIAutoLockWin);
		ui_SetFocus(wleUIAutoLockOk); // for being able to just press Enter
	}
}

/********************
* FIND AND REPLACE DIALOG
********************/
typedef struct WleUIFindAndReplaceWin
{
	UIWindow *win;
	UIComboBox *layer;
	UICheckButton *allLayers;
	UITextEntry *searchText, *replaceText;
	UITextEntry *prependText, *appendText;
	UIButton *okButton;
} WleUIFindAndReplaceWin;

typedef struct WleUIFindAndReplaceConfirmWin
{
	ZoneMapLayer **layers;
	UIButton *okButton;
	UIWindow *win;
	UIList *list;
} WleUIFindAndReplaceConfirmWin;

typedef struct WleUIFindAndReplaceSub
{
	bool use;
	GroupDef *origDef;
	char **newName;
	int newUid;
} WleUIFindAndReplaceSub;

static void wleUIFindAndReplaceSubFree(WleUIFindAndReplaceSub *sub)
{
	estrDestroy(sub->newName);
	free(sub);
}

static void wleUIFindAndReplaceConfirmCancelQueued(WleUIFindAndReplaceConfirmWin *ui)
{
	eaDestroy(&ui->layers);
	eaDestroyEx(ui->list->peaModel, wleUIFindAndReplaceSubFree);
	free(ui->list->peaModel);

	elUIWindowClose(NULL, ui->win);

	free(ui);
}

static bool wleUIFindAndReplaceConfirmCancel(UIWidget *widget, WleUIFindAndReplaceConfirmWin *ui)
{
	emQueueFunctionCall(wleUIFindAndReplaceConfirmCancelQueued, ui);
	return true;
}

static void wleUIFindAndReplaceConfirmOkQueued(WleUIFindAndReplaceConfirmWin *ui)
{
	StashTable subStash;
	int i;

	// make sure to only use the marked subs
	subStash = stashTableCreateAddress(16);
	for (i = 0; i < eaSize(ui->list->peaModel); i++)
	{
		WleUIFindAndReplaceSub *sub = ((WleUIFindAndReplaceSub**) *ui->list->peaModel)[i];
		if (sub->use)
			stashAddressAddInt(subStash, sub->origDef, sub->newUid, false);
	}

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&ui->layers); i++)
	{
		GroupTracker *tracker = layerGetTracker(ui->layers[i]);
		TrackerHandle *root;
		assert(tracker);
		root = trackerHandleCreate(tracker);
		assert(root);

		wleOpFindAndReplace(root, subStash);

		trackerHandleDestroy(root);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	
	// cleanup
	stashTableDestroy(subStash);
	wleUIFindAndReplaceConfirmCancelQueued(ui);
}

static void wleUIFindAndReplaceConfirmOk(UIWidget *widget, WleUIFindAndReplaceConfirmWin *ui)
{
	emQueueFunctionCall(wleUIFindAndReplaceConfirmOkQueued, ui);
}

static bool wleUIFindAndReplaceString(const char *text, const char *searchStr, const char *replaceStr, char **estrOutput)
{
	char *firstMatch = NULL;
	char *searchCopy, *textCopy;
	char *searchNext, *textNext;

	if (!searchStr || !text)
		return false;

	estrCopy2(estrOutput, text);
	strdup_alloca(textCopy, text);
	textNext = textCopy;
	strdup_alloca(searchCopy, searchStr);
	searchNext = strtok_r(searchCopy, "*", &searchCopy);

	if (searchStr[0] == '*')
		firstMatch = textCopy;
	do
	{
		char *match = strstri(textNext, searchNext);
		if (!match)
		{
			estrClear(estrOutput);
			return false;
		}
		
		if (!firstMatch)
			firstMatch = match;
		textNext = match + strlen(searchNext);

		searchNext = strtok_r(searchCopy, "*", &searchCopy);
	} while (searchNext && textNext);
	if (searchNext && !textNext)
	{
		estrClear(estrOutput);
		return false;
	}
	if (searchStr[strlen(searchStr) - 1] != '*')
		*textNext = '\0';

	estrReplaceOccurrences(estrOutput, firstMatch, replaceStr);
	return true;
}

static void wleUIFindAndReplaceGetSubs(GroupTracker *root, StashTable subs, const char *searchStr, const char *replaceStr, const char *prependStr, const char *appendStr)
{
	char *output = NULL;
	int i;

	if (root->def && wleUIFindAndReplaceString(root->def->name_str, searchStr, replaceStr, &output))
	{
		WleUIFindAndReplaceSub *sub = NULL;
		if (!stashAddressFindPointer(subs, root->def, &sub))
		{
			size_t search_len = strlen(searchStr);

			sub = calloc(1, sizeof(*sub));
			sub->origDef = root->def;
			sub->newName = calloc(1, sizeof(*sub->newName));
			estrCopy(sub->newName, &output);
			estrInsert(sub->newName, 0, prependStr, (int) strlen(prependStr));
			estrConcat(sub->newName, appendStr, (int) strlen(appendStr));
			sub->newUid = objectLibraryUIDFromObjName(*sub->newName);
			sub->use = !!sub->newUid;
			stashAddressAddPointer(subs, root->def, sub, false);
		}
	}
	else
	{
		for (i = 0; i < root->child_count; i++)
			wleUIFindAndReplaceGetSubs(root->children[i], subs, searchStr, replaceStr, prependStr, appendStr);
	}

	estrDestroy(&output);
}

static void wleUIFindAndReplaceConfirmValidate(WleUIFindAndReplaceConfirmWin *ui)
{
	WleUIFindAndReplaceSub **subs = (WleUIFindAndReplaceSub**) (*ui->list->peaModel);
	int i;
	
	for (i = 0; i < eaSize(&subs); i++)
	{
		if (subs[i]->use)
		{
			ui_SetActive(UI_WIDGET(ui->okButton), true);
			return;
		}
	}

	ui_SetActive(UI_WIDGET(ui->okButton), false);
}

static void wleUIFindAndReplaceConfirmSelectAll(UIButton *button, WleUIFindAndReplaceConfirmWin *ui)
{
	WleUIFindAndReplaceSub **subs = (WleUIFindAndReplaceSub**) (*ui->list->peaModel);
	int i;

	for (i = 0; i < eaSize(&subs); i++)
		if (subs[i]->newUid)
			subs[i]->use = true;

	wleUIFindAndReplaceConfirmValidate(ui);
}

static void wleUIFindAndReplaceConfirmDeselectAll(UIButton *button, WleUIFindAndReplaceConfirmWin *ui)
{
	WleUIFindAndReplaceSub **subs = (WleUIFindAndReplaceSub**) (*ui->list->peaModel);
	int i;

	for (i = 0; i < eaSize(&subs); i++)
		subs[i]->use = false;

	wleUIFindAndReplaceConfirmValidate(ui);
}

static void wleUIFindAndReplaceConfirmActivated(UIList *list, WleUIFindAndReplaceConfirmWin *ui)
{
	WleUIFindAndReplaceSub *sub = ui_ListGetSelectedObject(list);

	if (!sub)
		return;

	if (sub->newUid)
	{
		sub->use = !sub->use;
		wleUIFindAndReplaceConfirmValidate(ui);
	}
}

static void wleUIFindAndReplaceConfirmUseDraw(UIList *list, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int row, void *data)
{

	WleUIFindAndReplaceSub *sub = ((WleUIFindAndReplaceSub**) (*list->peaModel))[row];
	AtlasTex *tex = NULL;

	if (sub->use)
		tex = atlasLoadTexture("eui_tickybox_checked_8x8");
	else if (sub->newUid)
		tex = atlasLoadTexture("eui_tickybox_unchecked_8x8");

	if (tex)
		display_sprite(tex, x + 3, y + 2, z, scale, scale, 0xFFFFFFFF);
}

static void wleUIFindAndReplaceConfirmOldDefText(UIList *list, UIListColumn *column, S32 row, UserData unused, char **output)
{
	estrPrintf(output, "%s", ((WleUIFindAndReplaceSub**) (*list->peaModel))[row]->origDef->name_str);
}

static void wleUIFindAndReplaceConfirmNewDefText(UIList *list, UIListColumn *column, S32 row, UserData unused, char **output)
{
	WleUIFindAndReplaceSub *sub = ((WleUIFindAndReplaceSub**) (*list->peaModel))[row];
	estrPrintf(output, "%s%s%s", sub->newUid ? "" : "*", *sub->newName, sub->newUid ? "" : " (NOT VALID)");
}

static void wleUIFindAndReplaceDialogCancelQueued(WleUIFindAndReplaceWin *ui)
{
	eaDestroy(ui->layer->model);
	free(ui->layer->model);

	elUIWindowClose(NULL, ui->win);

	free(ui);
}

static bool wleUIFindAndReplaceDialogCancel(UIWidget *widget, WleUIFindAndReplaceWin *ui)
{
	emQueueFunctionCall(wleUIFindAndReplaceDialogCancelQueued, ui);
	return true;
}

static void wleUIFindAndReplaceDialogOkQueued(WleUIFindAndReplaceWin *ui)
{
	WleUIFindAndReplaceConfirmWin *confirmUi;
	WleUIFindAndReplaceSub ***subs;
	UIWindow *win;
	UIList *list;
	UIListColumn *column;
	UIButton *button;
	ZoneMapLayer *layer = ui_CheckButtonGetState(ui->allLayers) ? NULL : ui_ComboBoxGetSelectedObject(ui->layer);
	GroupTracker *tracker = layerGetTracker(layer);
	StashTable subStash;
	StashTableIterator iter;
	StashElement el;

	confirmUi = calloc(1, sizeof(*confirmUi));

	// first find all matches and tentative replacements
	subs = calloc(1, sizeof(*subs));
	subStash = stashTableCreateAddress(16);
	if (tracker)
	{
		eaPush(&confirmUi->layers, layer);
		wleUIFindAndReplaceGetSubs(tracker, subStash, ui_TextEntryGetText(ui->searchText), ui_TextEntryGetText(ui->replaceText), ui_TextEntryGetText(ui->prependText), ui_TextEntryGetText(ui->appendText));
	}
	else if (!layer)
	{
		const char *searchText = ui_TextEntryGetText(ui->searchText);
		const char *replaceText = ui_TextEntryGetText(ui->replaceText);
		const char *prependText = ui_TextEntryGetText(ui->prependText);
		const char *appendText = ui_TextEntryGetText(ui->appendText);
		int i;

		// gets subs across all editable layers
		for (i = 0; i < eaSize(ui->layer->model); i++)
		{
			ZoneMapLayer *tempLayer = (ZoneMapLayer*) (*ui->layer->model)[i];
			GroupTracker *tempTracker = layerGetTracker(tempLayer);
			if (tempTracker)
			{
				wleUIFindAndReplaceGetSubs(tempTracker, subStash, searchText, replaceText, prependText, appendText);
				eaPush(&confirmUi->layers, tempLayer);
			}
		}
	}
	stashGetIterator(subStash, &iter);
	while (stashGetNextElement(&iter, &el))
		eaPush(subs, stashElementGetPointer(el));
	stashTableDestroy(subStash);

	ui_WindowSetModal(ui->win, false);
	win = ui_WindowCreate("Confirm replacements", 0, 0, 400, 400);
	confirmUi->win = win;
	button = ui_ButtonCreate("Select all", 0, 0, wleUIFindAndReplaceConfirmSelectAll, confirmUi);
	button->widget.width = 0.5f;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.leftPad = button->widget.rightPad = 5;
	ui_WindowAddChild(win, button);
	button = ui_ButtonCreate("Deselect all", 0, 0, wleUIFindAndReplaceConfirmDeselectAll, confirmUi);
	button->widget.xPOffset = 0.5f;
	button->widget.width = 0.5f;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.leftPad = button->widget.rightPad = 5;
	ui_WindowAddChild(win, button);
	list = ui_ListCreate(NULL, subs, 15);
	list->widget.width = list->widget.height = 1;
	list->widget.widthUnit = list->widget.heightUnit = UIUnitPercentage;
	list->widget.topPad = elUINextY(button) + 5;
	list->widget.leftPad = list->widget.rightPad = 5;
	confirmUi->list = list;
	column = ui_ListColumnCreateCallback("", wleUIFindAndReplaceConfirmUseDraw, NULL);
	ui_ListColumnSetWidth(column, false, 15);
	ui_ListAppendColumn(list, column);
	column = ui_ListColumnCreate(UIListTextCallback, "Found definition", (intptr_t) wleUIFindAndReplaceConfirmOldDefText, NULL);
	ui_ListColumnSetWidth(column, false, 150);
	ui_ListAppendColumn(list, column);
	column = ui_ListColumnCreate(UIListTextCallback, "Replace with", (intptr_t) wleUIFindAndReplaceConfirmNewDefText, NULL);
	ui_ListColumnSetWidth(column, false, 150);
	ui_ListAppendColumn(list, column);
	ui_ListSetActivatedCallback(list, wleUIFindAndReplaceConfirmActivated, confirmUi);
	ui_WindowAddChild(win, list);

	button = elUIAddCancelOkButtons(win, wleUIFindAndReplaceConfirmCancel, confirmUi, wleUIFindAndReplaceConfirmOk, confirmUi);
	confirmUi->okButton = button;
	wleUIFindAndReplaceConfirmValidate(confirmUi);
	list->widget.bottomPad = elUINextY(button) + 10;
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);

	// destroy old window
	wleUIFindAndReplaceDialogCancelQueued(ui);
}

static void wleUIFindAndReplaceDialogOk(UIWidget *widget, WleUIFindAndReplaceWin *ui)
{
	emQueueFunctionCall(wleUIFindAndReplaceDialogOkQueued, ui);
}

static void wleUIFindAndReplaceLayerComboMakeText(UIComboBox *combo, S32 row, bool inBox, UserData unused, char **output)
{
	if (row >= 0 && row < eaSize(combo->model))
		estrPrintf(output, "%s", layerGetFilename(((ZoneMapLayer**) *combo->model)[row]));
}

void wleUIFindAndReplaceDialogEntryChanged(UITextEntry *entry, WleUIFindAndReplaceWin *ui)
{
	const char *searchText = ui_TextEntryGetText(ui->searchText);
	bool setActive = false;

	if (searchText[0] != '\0')
		setActive = true;

	ui_SetActive(UI_WIDGET(ui->okButton), setActive);
}

void wleUIFindAndReplaceDialogAllLayersToggled(UICheckButton *check, WleUIFindAndReplaceWin *ui)
{
	ui_SetActive(UI_WIDGET(ui->layer), !ui_CheckButtonGetState(check));
}

void wleUIFindAndReplaceDialogCreate(void)
{
	ZoneMapLayer ***lockedLayers = calloc(1, sizeof(*lockedLayers));
	int i;

	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (!layer)
			continue;
		if (layerGetLocked(layer) == 3)
			eaPush(lockedLayers, layer);
	}

	if (eaSize(lockedLayers) == 0)
	{
		UIDialog *dialog = ui_DialogCreate("Failed!", "You must have at least one locked layer in order to execute this operation.", NULL, NULL);
		elUICenterWindow(UI_WINDOW(dialog));
		ui_WindowShow(UI_WINDOW(dialog));
		eaDestroy(lockedLayers);
		free(lockedLayers);
	}
	else
	{
		WleUIFindAndReplaceWin *ui = calloc(1, sizeof(*ui));
		UIWindow *win = ui_WindowCreate("Find and Replace Geometry", 0, 0, 400, 0);
		UIComboBox *combo;
		UICheckButton *check;
		UILabel *label;
		UITextEntry *entry;
		UIButton *button;
		int leftPad = 0;

		check = ui_CheckButtonCreate(5, 5, "Search all editable layers", true);
		ui_CheckButtonSetToggledCallback(check, wleUIFindAndReplaceDialogAllLayersToggled, ui);
		ui_WindowAddChild(win, check);
		ui->allLayers = check;
		label = ui_LabelCreate("Layer", 25, elUINextY(check));
		ui_WindowAddChild(win, label);
		combo = ui_ComboBoxCreate(0, label->widget.y, 1, NULL, lockedLayers, NULL);
		ui_ComboBoxSetTextCallback(combo, wleUIFindAndReplaceLayerComboMakeText, NULL);
		ui_ComboBoxSetSelected(combo, 0);
		combo->widget.widthUnit = UIUnitPercentage;
		combo->widget.rightPad = 5;
		ui->layer = combo;
		ui_WindowAddChild(win, combo);
		label = ui_LabelCreate("Search for", 5, elUINextY(label) + 15);
		ui_WindowAddChild(win, label);
		entry = ui_TextEntryCreate("", 0, label->widget.y);
		ui_TextEntrySetChangedCallback(entry, wleUIFindAndReplaceDialogEntryChanged, ui);
		ui_TextEntrySetEnterCallback(entry, wleUIFindAndReplaceDialogOk, ui);
		entry->widget.width = 1;
		entry->widget.widthUnit = UIUnitPercentage;
		entry->widget.rightPad = 5;
		ui->searchText = entry;
		ui_WindowAddChild(win, entry);
		label = ui_LabelCreate("Replace with", 5, elUINextY(label) + 15);
		ui_WindowAddChild(win, label);
		leftPad = combo->widget.leftPad = entry->widget.leftPad = elUINextX(label) + 5;
		entry = ui_TextEntryCreate("", 0, label->widget.y);
		ui_TextEntrySetChangedCallback(entry, wleUIFindAndReplaceDialogEntryChanged, ui);
		ui_TextEntrySetEnterCallback(entry, wleUIFindAndReplaceDialogOk, ui);
		entry->widget.width = 1;
		entry->widget.widthUnit = UIUnitPercentage;
		entry->widget.leftPad = elUINextX(label) + 5;
		entry->widget.rightPad = 5;
		ui->replaceText = entry;
		ui_WindowAddChild(win, entry);
		label = ui_LabelCreate("Prepend", 25, elUINextY(label));
		ui_WindowAddChild(win, label);
		entry = ui_TextEntryCreate("", 0, label->widget.y);
		ui_TextEntrySetChangedCallback(entry, wleUIFindAndReplaceDialogEntryChanged, ui);
		ui_TextEntrySetEnterCallback(entry, wleUIFindAndReplaceDialogOk, ui);
		entry->widget.width = 1;
		entry->widget.widthUnit = UIUnitPercentage;
		entry->widget.leftPad = leftPad;
		entry->widget.rightPad = 5;
		ui->prependText = entry;
		ui_WindowAddChild(win, entry);
		label = ui_LabelCreate("Append", 25, elUINextY(label));
		ui_WindowAddChild(win, label);
		entry = ui_TextEntryCreate("", 0, label->widget.y);
		ui_TextEntrySetChangedCallback(entry, wleUIFindAndReplaceDialogEntryChanged, ui);
		ui_TextEntrySetEnterCallback(entry, wleUIFindAndReplaceDialogOk, ui);
		entry->widget.width = 1;
		entry->widget.widthUnit = UIUnitPercentage;
		entry->widget.leftPad = leftPad;
		entry->widget.rightPad = 5;
		ui->appendText = entry;
		ui_WindowAddChild(win, entry);
		ui->win = win;
		button = elUIAddCancelOkButtons(win, wleUIFindAndReplaceDialogCancel, ui, wleUIFindAndReplaceDialogOk, ui);
		ui->okButton = button;
		ui_SetActive(UI_WIDGET(button), false);
		win->widget.height = elUINextY(label) + elUINextY(button) + 5;
		elUICenterWindow(win);
		ui_WindowSetCloseCallback(win, wleUIFindAndReplaceDialogCancel, ui);
		ui_WindowSetModal(win, true);
		ui_WindowShow(win);

		// update state
		wleUIFindAndReplaceDialogAllLayersToggled(check, ui);
	}
}

/********************
* REGION MANAGER
********************/
typedef struct WleUIRegionEditorWin
{
	WleSettingsRegionMngrRegion *region;
	UIWindow *window;
	UIList *overlapList;
	UITextEntry *regionName;
	UICheckButton *tint;
	UIColorButton *color;
} WleUIRegionEditorWin;

static void wleUINewRegionDialogCreate(const char *layerFilename);
static void wleUIRegionEditorCreate(WleSettingsRegionMngrRegion *regionSettings);

static void wleUIRegionMngrTreeSetWidth(void)
{
	AtlasTex *opened = (g_ui_Tex.minus);
	UITree *tree = editorUIState->regionMngrUI.tree;
	int i, j;
	F32 maxWidth = 0;
	UIStyleFont *font = GET_REF(g_ui_State.font);
	GfxFont *drawContext = NULL;

	if (font)
		drawContext = GET_REF(font->hFace);
	if (!drawContext)
		return;

	// iterate through region nodes
	for (i = 0; i < eaSize(&tree->root.children); i++)
	{
		UITreeNode *regionNode = tree->root.children[i];

		// iterate through layers in each region
		for (j = 0; j < eaSize(&regionNode->children); j++)
		{
			const char *c = strrchr(layerGetFilename(regionNode->children[j]->contents), '/');
			F32 stringWidth = 0;
			
			if (c)
				c++;
			else
				c = layerGetFilename(regionNode->children[j]->contents);

			stringWidth = UI_STEP + opened->width + gfxfont_StringWidthf(drawContext, 1, 1, "%s", c);
			maxWidth = MAX(maxWidth, stringWidth);
		}
	}

	tree->width = maxWidth;
}

/******
* This function returns the region settings object for a particular region name.  If it doesn't exist,
* it gets created.
* PARAMS:
*   regionName - string region name to create
*******/
static WleSettingsRegionMngrRegion *wleUIRegionMngrSettingsRegionFind(const char *regionName)
{
	WleSettingsRegionMngrRegion *regionSettings;
	int i;

	for (i = 0; i < eaSize(&editorUIState->regionMngrUI.regionMngr->regions); i++)
	{
		if (editorUIState->regionMngrUI.regionMngr->regions[i]->regionName
		 && strcmpi(editorUIState->regionMngrUI.regionMngr->regions[i]->regionName, regionName) == 0)
		{
			return editorUIState->regionMngrUI.regionMngr->regions[i];
		}
	}
	
	// if not found, create a new settings object
	regionSettings = StructCreate(parse_WleSettingsRegionMngrRegion);
	regionSettings->regionName = StructAllocString(regionName);
	regionSettings->tint = false;
	regionSettings->argb = 0x3F000000;
	regionSettings->sky_names = NULL;

	// register the settings object
	eaPush(&editorUIState->regionMngrUI.regionMngr->regions, regionSettings);

	return regionSettings;
}

/******
* This function destroys the region settings for all regions without layers (except the Default settings).
******/
static void wleUIRegionMngrSettingsPurge(void)
{
	WleSettingsRegionMngrRegion **existingSettings = NULL;
	int i, j;

	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		const char *regionName = layerGetWorldRegionString(layer);

		if (!layer)
			continue;

		if (!regionName || strlen(regionName) == 0)
			regionName = "Default";
	
		for (j = 0; j < eaSize(&editorUIState->regionMngrUI.regionMngr->regions); j++)
		{
			if (editorUIState->regionMngrUI.regionMngr->regions[j]->regionName && regionName)
			{
				ANALYSIS_ASSUME(editorUIState->regionMngrUI.regionMngr->regions[j]->regionName && regionName);
				if (!strcmpi(editorUIState->regionMngrUI.regionMngr->regions[j]->regionName, regionName))
				{
					eaPushUnique(&existingSettings, editorUIState->regionMngrUI.regionMngr->regions[j]);
				}
			}
		}
	}

	for (i = eaSize(&editorUIState->regionMngrUI.regionMngr->regions) - 1; i >= 0; i--)
	{
		// destroy settings that were not being used
		if (eaFind(&existingSettings, editorUIState->regionMngrUI.regionMngr->regions[i]) == -1)
		{
			if ((editorUIState->regionMngrUI.regionMngr->regions[i])->regionName)
			{
				ANALYSIS_ASSUME((editorUIState->regionMngrUI.regionMngr->regions[i])->regionName);
				if (strcmpi((editorUIState->regionMngrUI.regionMngr->regions[i])->regionName, "Default") != 0)
				{
					WleSettingsRegionMngrRegion *removedRegion = editorUIState->regionMngrUI.regionMngr->regions[i];
					StructDestroy(parse_WleSettingsRegionMngrRegion, eaRemove(&editorUIState->regionMngrUI.regionMngr->regions, i));
				}
			}
		}
	}

	EditorPrefStoreStruct(WLE_PREF_EDITOR_NAME, "RegionMngr", zmapGetFilename(NULL), parse_WleSettingsRegionMngr, editorUIState->regionMngrUI.regionMngr);
}

/******
* This function should be called to load the region manager's settings for the current zonemap.
******/
void wleUIRegionMngrSettingsInit(void)
{
	// clear anything that might be holding pointers to these settings
	if (editorUIState->regionMngrUI.sky_list)
		ui_ListSetModel(editorUIState->regionMngrUI.sky_list, NULL, NULL);

	// initialize struct
	StructDestroy(parse_WleSettingsRegionMngr, editorUIState->regionMngrUI.regionMngr);
	editorUIState->regionMngrUI.regionMngr = StructCreate(parse_WleSettingsRegionMngr);
	EditorPrefGetStruct(WLE_PREF_EDITOR_NAME, "RegionMngr", zmapGetFilename(NULL), parse_WleSettingsRegionMngr, editorUIState->regionMngrUI.regionMngr);

	wleUIRegionMngrSettingsPurge();
}

/******
* This function returns a CRC value for a string.
* PARAMS:
*   str - string to use
* RETURNS:
*   int CRC value corresponding to the string
******/
static int wleUIRegionMngrGetStringCRC(const char *str)
{
	cryptAdler32Init();
	cryptAdler32UpdateString(str);
	return cryptAdler32Final();
}

// UI CALLBACKS
// new region OK button clicked
static void wleUINewRegionOkClicked(UIWidget *unused, UITextEntry *entry)
{
	const char *regionName = ui_TextEntryGetText(entry);
	const char **layerNames = NULL;

	// validate entry
	if (!regionName || strlen(regionName) == 0)
	{
		emStatusPrintf("Please specify a region name!");
		return;
	}

	if (strcmpi(regionName, "Default") == 0)
		regionName = NULL;
	eaPush(&layerNames, editorUIState->name);
	wleOpSetLayerRegion(layerNames, regionName);
	elUIWindowClose(NULL, editorUIState->currModalWin);
	SAFE_FREE(editorUIState->name);
	eaDestroy(&layerNames);

	wleUIRegionMngrSettingsPurge();
}

// region settings right-click menu item callback
static void wleUIRegionMngrTreeRegionEdit(UIMenuItem *item, WleSettingsRegionMngrRegion *regionSettings)
{
	wleUIRegionEditorCreate(regionSettings);
}

// region center camera right-click menu item callback
static void wleUIRegionMngrTreeRegionCenterCam(UIMenuItem *item, WleSettingsRegionMngrRegion *regionSettings)
{
	char *regionName = regionSettings->regionName;
	GfxCameraController *camera = gfxGetActiveCameraController();
	WorldRegion *region;
	Vec3 min, max, center;
	
	if (strcmpi(regionName, "Default") == 0)
		regionName = NULL;
	region = zmapGetWorldRegionByName(NULL, regionSettings->regionName);
	if(worldRegionGetBounds(region, min, max))
	{
		addVec3(min, max, center);
		scaleVec3(center, 0.5, center);
		gfxCameraControllerSetTarget(camera, center);
		camera->camdist = 0;
	}
}

// layer create new region right-click menu item callback
static void wleUIRegionMngrTreeLayerNewRegion(UIMenuItem *item, ZoneMapLayer *layer)
{
	wleUINewRegionDialogCreate(layerGetFilename(layer));
}

// layer move to region right-click menu item callback
static void wleUIRegionMngrTreeLayerMove(UIMenuItem *item, ZoneMapLayer *layer)
{
	const char **layerNames = NULL;
	eaPush(&layerNames, layerGetFilename(layer));
	wleOpSetLayerRegion(layerNames, ui_MenuItemGetText(item));
	eaDestroy(&layerNames);

	wleUIRegionMngrSettingsPurge();
}

// layer select overlap right-click menu item callback
static void wleUIRegionMngrTreeLayerOverlapSelect(UIMenuItem *item, ZoneMapLayer *layer)
{
	const char *regionName = ui_MenuItemGetText(item);
	WorldRegion *region;
	GroupTracker *layerTracker = layerGetTracker(layer);
	Vec3 regionMin, regionMax;
	int i;

	if (regionName)
		if(strlen(regionName) == 0 || strcmpi(regionName, "Default") == 0)
			regionName = NULL;
	region = zmapGetWorldRegionByName(NULL, regionName);

	if (!region)
		return;

	if (!layerTracker)
		return;

	if(worldRegionGetBounds(region, regionMin, regionMax))
	{
		TrackerHandle **eaSelectHandles = NULL;
		wleTrackerDeselectAll();
		for (i = 0; i < layerTracker->child_count; i++)
		{
			GroupTracker *child = layerTracker->children[i];
			if (child && child->def)
			{
				Mat4 childMat;
				Vec3 boundsMin, boundsMax;

				trackerGetMat(child, childMat);
				mulBoundsAA(child->def->bounds.min, child->def->bounds.max, childMat, boundsMin, boundsMax);
				if (boxBoxCollision(regionMin, regionMax, boundsMin, boundsMax))
				{
					eaPush(&eaSelectHandles, trackerHandleFromTracker(child));
				}
			}
		}
		wleTrackerSelectList(eaSelectHandles, true);
		eaDestroy(&eaSelectHandles);
	}
}

// tint all button clicked
static void wleUIRegionMngrTintAllClicked(UIButton *button, UserData unused)
{
	int i;

	for (i = 0; i < eaSize(&editorUIState->regionMngrUI.regionMngr->regions); i++)
		editorUIState->regionMngrUI.regionMngr->regions[i]->tint = true;
	EditorPrefStoreStruct(WLE_PREF_EDITOR_NAME, "RegionMngr", zmapGetFilename(NULL), parse_WleSettingsRegionMngr, editorUIState->regionMngrUI.regionMngr);
}

// untint all button clicked
static void wleUIRegionMngrUntintAllClicked(UIButton *button, UserData unused)
{
//	WleSettingsRegionMngrRegion ***regions;

	int i;

	for (i = 0; i < eaSize(&editorUIState->regionMngrUI.regionMngr->regions); i++)
		editorUIState->regionMngrUI.regionMngr->regions[i]->tint = false;
	EditorPrefStoreStruct(WLE_PREF_EDITOR_NAME, "RegionMngr", zmapGetFilename(NULL), parse_WleSettingsRegionMngr, editorUIState->regionMngrUI.regionMngr);
}

// tree layer draw callback
static void wleUIRegionMngrLayerNodeDisplay(UITreeNode *region, ZoneMapLayer *layer, UI_MY_ARGS, F32 z)
{	
	const char *c = strrchr(layerGetFilename(layer), '/');

	if (c)
		c++;
	else
		c = layerGetFilename(layer);

	ui_TreeDisplayText(region, c, UI_MY_VALUES, z);
}

// tree region free
static void wleUIRegionMngrRegionNodeFree(UITreeNode *region)
{
	eaDestroy(region->contents);
	free(region->contents);
}

// tree region draw callback
static void wleUIRegionMngrRegionNodeDisplay(UITreeNode *region, WleSettingsRegionMngrRegion *regionSettings, UI_MY_ARGS, F32 z)
{
	CBox box = {x, y, x + w, y + h};
	UIStyleFont *font = ui_StyleFontGet("WorldEditor_Region");
	U32 rgba = (regionSettings->argb << 8) | 0x000000FF;
	
	if (font)
	{
		font->uiColor = rgba;
		ui_StyleFontUse(font, region == region->tree->selected || (region->tree->multiselect && eaFind(&region->tree->multiselected, region)), UI_WIDGET(region->tree)->state);
		clipperPushRestrict(&box);
		gfxfont_Printf(x, y + h / 2, z, scale, scale, CENTER_Y, "%s%s%s", regionSettings->tint ? "" : "(",
			regionSettings->regionName, regionSettings->tint ? "" : ")");
		clipperPop();
	}
}

// tree region fill callback
static void wleUIRegionMngrRegionNodeFill(UITreeNode *region, ZoneMapLayer ***regionLayers)
{
	int i;
	for (i = 0; i < eaSize(regionLayers); i++)
	{
		ui_TreeNodeAddChild(region, ui_TreeNodeCreate(region->tree, wleUIRegionMngrGetStringCRC(layerGetFilename((*regionLayers)[i])), NULL, (*regionLayers)[i],
			NULL, NULL, wleUIRegionMngrLayerNodeDisplay, (*regionLayers)[i], 15));
	}
	if (i)
		wleUIRegionMngrTreeSetWidth();
}

// tree root fill callback
static void wleUIRegionMngrRootFill(UITreeNode *root, UserData unused)
{
	int i;
	StashTable regionStash = stashTableCreateWithStringKeys(16, StashDefault);
	StashTableIterator iter;
	StashElement el;
	ZoneMapLayer ***regionLayers;
	bool addedNode = false;

	// enumerate all regions being used and which layers belong to them
	stashAddPointer(regionStash, allocAddString("Default"), calloc(1, sizeof(ZoneMapLayer**)), false);
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		const char *regionName;
		
		if (!layer)
			continue;
		regionName = layerGetWorldRegionString(layer);
		if (!regionName || strlen(regionName) == 0)
			regionName = "Default";
		if (!stashFindPointer(regionStash, regionName, &((void*) regionLayers)))
		{
			regionLayers = calloc(1, sizeof(ZoneMapLayer**));
			stashAddPointer(regionStash, regionName, regionLayers, false);
		}
		eaPush(regionLayers, layer);
	}

	// create child nodes for each region found
	// default region
	if (stashFindPointer(regionStash, "Default", &((void*) regionLayers)))
	{
		UITreeNode *newNode = ui_TreeNodeCreate(root->tree, wleUIRegionMngrGetStringCRC("Default"), NULL, (void*) regionLayers, wleUIRegionMngrRegionNodeFill, (void*) regionLayers,
			wleUIRegionMngrRegionNodeDisplay, wleUIRegionMngrSettingsRegionFind("Default"), 15);
		newNode->freeF = wleUIRegionMngrRegionNodeFree;
		ui_TreeNodeAddChild(root, newNode);
		addedNode = true;
	}

	// all other regions
	stashGetIterator(regionStash, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		const char *regionName = stashElementGetKey(el);
		UITreeNode *newNode;

		if (strcmpi(regionName, "Default") == 0)
			continue;
		regionLayers = stashElementGetPointer(el);
		newNode = ui_TreeNodeCreate(root->tree, wleUIRegionMngrGetStringCRC(regionName), NULL, (void*) regionLayers, wleUIRegionMngrRegionNodeFill, (void*) regionLayers,
			wleUIRegionMngrRegionNodeDisplay, wleUIRegionMngrSettingsRegionFind(regionName), 15);
		newNode->freeF = wleUIRegionMngrRegionNodeFree;
		ui_TreeNodeAddChild(root, newNode);
		addedNode = true;
	}

	if (addedNode)
		wleUIRegionMngrTreeSetWidth();

	// cleanup
	stashTableDestroy(regionStash);
}

// tree right click callback
static void wleUIRegionMngrTreeRClick(UITree *tree, UserData unused)
{
	UITreeNode *currNode = ui_TreeGetSelected(tree);

	if (!currNode)
		return;

	// clear and free old menu items
	if (!editorUIState->regionMngrUI.rclickMenu)
		editorUIState->regionMngrUI.rclickMenu = ui_MenuCreate("");
	eaDestroyEx(&editorUIState->regionMngrUI.rclickMenu->items, ui_MenuItemFree);

	// region nodes right-clicked
	if (currNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = currNode->displayData;
		ui_MenuAppendItems(editorUIState->regionMngrUI.rclickMenu,
			ui_MenuItemCreate("Region info...", UIMenuCallback, wleUIRegionMngrTreeRegionEdit, regionSettings, NULL),
			ui_MenuItemCreate("Center camera", UIMenuCallback, wleUIRegionMngrTreeRegionCenterCam, regionSettings, NULL),
			NULL);
	}
	// layer nodes right-clicked
	else if (currNode->displayF == wleUIRegionMngrLayerNodeDisplay)
	{
		ZoneMapLayer *layer = currNode->contents;
		UIMenu *moveSubmenu;
		UIMenu *selectSubmenu = NULL;
		GroupTracker *layerTracker;
		int i;

		// clear submenus
		moveSubmenu = ui_MenuCreate("");
		selectSubmenu = ui_MenuCreate("");

		// create "move to..." submenu
		ui_MenuAppendItem(moveSubmenu, ui_MenuItemCreate("New region...", UIMenuCallback, wleUIRegionMngrTreeLayerNewRegion, layer, NULL));
		for (i = 0; i < eaSize(&editorUIState->regionMngrUI.regionMngr->regions); i++)
		{
			int j;

			// append menu item for regions that have at least one layer belonging to it
			for (j = 0; j < zmapGetLayerCount(NULL); j++)
			{
				const char *layerRegion = layerGetWorldRegionString(zmapGetLayer(NULL, j));
				if (!layerRegion)
					layerRegion = "Default";
				if (strcmpi(layerRegion, (editorUIState->regionMngrUI.regionMngr->regions[i])->regionName) == 0)
				{
					ui_MenuAppendItem(moveSubmenu, ui_MenuItemCreate(editorUIState->regionMngrUI.regionMngr->regions[i]->regionName, UIMenuCallback, wleUIRegionMngrTreeLayerMove, layer, NULL));
					break;
				}
			}
		}

		// create "select overlap" submenu for editable grouptree layers
		if (selectSubmenu && (layerTracker = layerGetTracker(layer)))
		{
			WorldRegion **foundRegions = NULL;
			WorldRegion *layerRegion = layerGetWorldRegion(layer);

			assert(layerTracker->def);

			for (i = 0; i < zmapGetLayerCount(NULL); i++)
			{
				const char *regionName = layerGetWorldRegionString(zmapGetLayer(NULL, i));
				WorldRegion *region = zmapGetWorldRegionByName(NULL, regionName);
				Vec3 min, max;

				// skip regions that have already been considered
				if (layerRegion == region || eaFind(&foundRegions, region) != -1)
					continue;

				// if region overlaps layer bounds, create menu item for the region
				eaPush(&foundRegions, region);
				if(worldRegionGetBounds(region, min, max))
					if (boxBoxCollision(min, max, layerTracker->def->bounds.min, layerTracker->def->bounds.max))
						ui_MenuAppendItem(selectSubmenu, ui_MenuItemCreate(regionName ? regionName : "Default", UIMenuCallback, wleUIRegionMngrTreeLayerOverlapSelect, layer, NULL));
			}
			eaDestroy(&foundRegions);
		}

		// add common layer menu items
		wleUILayerMenuItemsAdd(editorUIState->regionMngrUI.rclickMenu, layer);

		ui_MenuAppendItems(editorUIState->regionMngrUI.rclickMenu,
			ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
			ui_MenuItemCreate("Move to...", UIMenuSubmenu, NULL, NULL, moveSubmenu),
			NULL);
		if (selectSubmenu)
		{
			UIMenuItem *item = ui_MenuItemCreate("Select overlap with...", UIMenuSubmenu, NULL, NULL, selectSubmenu);
			item->active = (layerGetTracker(layer) != NULL);
			ui_MenuAppendItem(editorUIState->regionMngrUI.rclickMenu, item);
		}
	}
	editorUIState->regionMngrUI.rclickMenu->widget.scale = emGetSidebarScale() / g_ui_State.scale;
	ui_MenuPopupAtCursor(editorUIState->regionMngrUI.rclickMenu);
}

// tree activated callback
static void wleUIRegionMngrTreeActivated(UITree *tree, UserData unused)
{
	UITreeNode *activatedNode = ui_TreeGetSelected(tree);

	if (!activatedNode)
		return;

	// region nodes activated
	if (activatedNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = activatedNode->displayData;
		regionSettings->tint = !regionSettings->tint;
		EditorPrefStoreStruct(WLE_PREF_EDITOR_NAME, "RegionMngr", zmapGetFilename(NULL), parse_WleSettingsRegionMngr, editorUIState->regionMngrUI.regionMngr);
	}
	// layer nodes activated
	else if (activatedNode->displayF == wleUIRegionMngrLayerNodeDisplay)
	{
	}
}

// tree selected callback
static void wleUIRegionMngrTreeSelected(UITree *tree, UIList *sky_list)
{
	UITreeNode *selectedNode = ui_TreeGetSelected(tree);
	int i;

	// region nodes selected
	if (selectedNode && selectedNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = selectedNode->displayData;
		ZoneMap *zmap = worldGetActiveMap();
		WorldRegion *region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);
		SkyInfoGroup *sky_group = region ? worldRegionGetSkyGroup(region):NULL;
		WorldRegionType region_type = region ? worldRegionGetType(region):WRT_Ground;
		char tempStr[256];
		const char *** currentModel;
		bool refreshSkyList = true;

		tempStr[0] = 0;

		eaClearEx(&regionSettings->sky_names, NULL);
		if (sky_group)
		{
			for (i = 0; i < eaSize(&sky_group->override_list); ++i)
			{
				const char *sky_name = REF_STRING_FROM_HANDLE(sky_group->override_list[i]->sky);
				if (sky_name)
					eaPush(&regionSettings->sky_names, strdup(sky_name));
			}
		}

		currentModel = (const char***)ui_ListGetModel(sky_list);
		if (currentModel && eaSize(currentModel) == eaSize(&regionSettings->sky_names))
		{
			refreshSkyList = false;
			EARRAY_FOREACH_BEGIN(regionSettings->sky_names, n);
			{
				if (strcmp((*currentModel)[n],regionSettings->sky_names[n]))
				{
					refreshSkyList = true;
					break;
				}
			}
			EARRAY_FOREACH_END;
		}

		ui_ListSetSelectedCallback(sky_list, NULL, regionSettings);
		ui_ListSetModel(sky_list, NULL, &regionSettings->sky_names);
		if (refreshSkyList)
			ui_ListClearEverySelection(sky_list);

		ui_SetActive(UI_WIDGET(editorUIState->regionMngrUI.cubemapOverrideButton), true);
		ui_CheckButtonSetState(editorUIState->regionMngrUI.clusterWorldGeoButton, region ? region->bWorldGeoClustering : 0);
		ui_CheckButtonSetState(editorUIState->regionMngrUI.indoorLightingButton, region ? worldRegionGetIndoorLighting(region) : 0);
		if (region)
			sprintf(tempStr, "<img src=\"%s\" width=50 height=50>", worldRegionGetOverrideCubeMap(region));
		ui_ButtonSetText(editorUIState->regionMngrUI.cubemapOverrideButton, region ? worldRegionGetOverrideCubeMap(region) : "");
		ui_ButtonSetTooltip(editorUIState->regionMngrUI.cubemapOverrideButton, tempStr);
		sprintf(tempStr, "%i", region ? worldRegionGetAllowedPetsPerPlayer(region) : 0);
		ui_TextEntrySetText(editorUIState->regionMngrUI.maxPetsEntry, region ? tempStr : "");
		ui_TextEntrySetText(editorUIState->regionMngrUI.vehicleRulesEntry, region ? StaticDefineIntRevLookup(VehicleRulesEnum,worldRegionGetVehicleRules(region)) : "");

		for (i = 0; i < eaSize(&editorUIState->regionMngrUI.typeRadioGroup->buttons); ++i)
			editorUIState->regionMngrUI.typeRadioGroup->buttons[i]->state = i == region_type;
	}
	// layer nodes or no node selected
	else
	{
		// clear all widgets
		ui_ListSetSelectedCallback(sky_list, NULL, NULL);
		ui_ListSetModel(sky_list, NULL, NULL);
		ui_ListClearEverySelection(sky_list);

		ui_ButtonSetTooltip(editorUIState->regionMngrUI.cubemapOverrideButton, "");
		ui_ButtonSetText(editorUIState->regionMngrUI.cubemapOverrideButton, "");
		ui_SetActive(UI_WIDGET(editorUIState->regionMngrUI.cubemapOverrideButton), false);
		ui_CheckButtonSetState(editorUIState->regionMngrUI.clusterWorldGeoButton, 0);
		ui_CheckButtonSetState(editorUIState->regionMngrUI.indoorLightingButton, 0);
		ui_TextEntrySetText(editorUIState->regionMngrUI.maxPetsEntry, "");
		ui_TextEntrySetText(editorUIState->regionMngrUI.vehicleRulesEntry, "");

		for (i = 0; i < eaSize(&editorUIState->regionMngrUI.typeRadioGroup->buttons); ++i)
			editorUIState->regionMngrUI.typeRadioGroup->buttons[i]->state = false;
	}
}

static bool wleUIRegionMngrOverrideCubemapTextureSelected(EMPicker* picker, EMPickerSelection **texNames, void *unused)
{
	UITreeNode *selectedNode = ui_TreeGetSelected(editorUIState->regionMngrUI.tree);

	// region node is selected
	if (eaSize(&texNames) == 1 && selectedNode && selectedNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = selectedNode->displayData;
		ZoneMap *zmap = worldGetActiveMap();
		WorldRegion *region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);
		char texName[MAX_PATH];
		BasicTexture *texture;

		getFileNameNoExt(texName, texNames[0]->doc_name);
		texture = texFindAndFlag(texName, false, WL_FOR_WORLD);
		if (!texture)
		{
			emStatusPrintf("Could not find texture \"%s\".", texName);
			return false;
		}
		else if (!texIsCubemap(texture))
		{
			emStatusPrintf("\"%s\" is not a cubemap texture.", texName);
			return false;
		}
		else if (region)
			wleOpSetRegionOverrideCubemap(region, texName);
	}

	return true;
}

static void wleUIRegionMngrOverrideCubemapClicked(UIButton *button, void *unused)
{
	EMPicker* texPicker = emPickerGetByName("Texture Picker");
	if (texPicker)
		emPickerShow(texPicker, "Select cubemap override", false, wleUIRegionMngrOverrideCubemapTextureSelected, NULL);
}

static void wleUIRegionMngrMaxPetsChanged(UITextEntry *entry, void *unused)
{
	UITreeNode *selectedNode = ui_TreeGetSelected(editorUIState->regionMngrUI.tree);

	// region node is selected
	if (selectedNode && selectedNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = selectedNode->displayData;
		ZoneMap *zmap = worldGetActiveMap();
		WorldRegion *region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);

		if (region)
			wleOpSetRegionMaxPets(region, atoi(ui_TextEntryGetText(entry)));
	}
}

static void wleUIRegionMngrClusterWorldGeoChanged(UICheckButton *check, void *unused)
{
	UITreeNode *selectedNode = ui_TreeGetSelected(editorUIState->regionMngrUI.tree);
	
	// region node is selected
	if (selectedNode && selectedNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = selectedNode->displayData;
		ZoneMap *zmap = worldGetActiveMap();
		WorldRegion *region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);

		if (region) {
			wleOpSetRegionClusterWorldGeo(region, ui_CheckButtonGetState(check));
			zmapTrackerUpdate(zmap, true, false);
		}
	}
}

static void wleUIRegionMngrVehicleRulesChanged(UITextEntry *entry, void *unused)
{
	UITreeNode *selectedNode = ui_TreeGetSelected(editorUIState->regionMngrUI.tree);

	// region node is selected
	if (selectedNode && selectedNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = selectedNode->displayData;
		ZoneMap *zmap = worldGetActiveMap();
		WorldRegion *region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);

		if (region)
			wleOpSetVehicleRulesChanged(region, StaticDefineIntGetInt(VehicleRulesEnum,ui_TextEntryGetText(entry)));
	}
}

static void wleUIRegionMngrIndoorLightingChanged(UICheckButton *check, void *unused)
{
	UITreeNode *selectedNode = ui_TreeGetSelected(editorUIState->regionMngrUI.tree);

	// region node is selected
	if (selectedNode && selectedNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = selectedNode->displayData;
		ZoneMap *zmap = worldGetActiveMap();
		WorldRegion *region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);

		if (region)
			wleOpSetRegionIndoorLighting(region, ui_CheckButtonGetState(check));
	}
}

static void wleUIRegionMngrTypeToggled(UIRadioButton *radio, void *unused)
{
	UITreeNode *activatedNode = ui_TreeGetSelected(editorUIState->regionMngrUI.tree);

	if (!activatedNode || !radio->state)
		return;

	// region nodes selected
	if (activatedNode->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		WleSettingsRegionMngrRegion *regionSettings = activatedNode->displayData;
		ZoneMap *zmap = worldGetActiveMap();
		WorldRegion *region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);
		int i = eaFind(&editorUIState->regionMngrUI.typeRadioGroup->buttons, radio);
		if (i >= 0 && i < WRT_COUNT && region)
			wleOpSetRegionType(region, i);
	}
}

static bool wleUIRegionMngrAddSkyFromPicker(EMPicker *picker, EMPickerSelection **selections, WleSettingsRegionMngrRegion *regionSettings)
{
	if (selections)
	{
		ZoneMap *zmap = worldGetActiveMap();
		WorldRegion *region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);
		SkyInfoGroup *sky_group;
		int i;

		eaPush(&regionSettings->sky_names, strdup(selections[0]->doc_name));

		sky_group = StructCreate(parse_SkyInfoGroup);
		for (i = 0; i < eaSize(&regionSettings->sky_names); ++i)
			gfxSkyGroupAddOverride(sky_group, regionSettings->sky_names[i]);
		wleOpSetRegionSkyGroup(region, sky_group);
		StructDestroy(parse_SkyInfoGroup, sky_group);
	}

	return true;
}

static void wleUIRegionMngrAddSky(UIButton *button, UIList *sky_list)
{
	WleSettingsRegionMngrRegion *regionSettings = sky_list->pSelectedData;

	if (!regionSettings)
		return;

	ui_ListClearEverySelection(sky_list);
	emPickerShow(&skyPicker, "Add", false, wleUIRegionMngrAddSkyFromPicker, regionSettings);
}

static void wleUIRegionMngrRemoveSky(UIButton *button, UIList *sky_list)
{
	WleSettingsRegionMngrRegion *regionSettings = sky_list->pSelectedData;
	char *sky_name = ui_ListGetSelectedObject(sky_list);
	SkyInfoGroup *sky_group;
	WorldRegion *region;
	ZoneMap *zmap;
	int i;

	if (!regionSettings || !sky_name)
		return;

	eaRemove(&regionSettings->sky_names, ui_ListGetSelectedRow(sky_list));
	free(sky_name);

	zmap = worldGetActiveMap();
	region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);
	sky_group = StructCreate(parse_SkyInfoGroup);
	for (i = 0; i < eaSize(&regionSettings->sky_names); ++i)
		gfxSkyGroupAddOverride(sky_group, regionSettings->sky_names[i]);
	wleOpSetRegionSkyGroup(region, sky_group);
	StructDestroy(parse_SkyInfoGroup, sky_group);
}

static void wleUIRegionMngrMoveSkyUp(UIButton *button, UIList *sky_list)
{
	WleSettingsRegionMngrRegion *regionSettings = sky_list->pSelectedData;
	char *sky_name = ui_ListGetSelectedObject(sky_list);
	SkyInfoGroup *sky_group;
	WorldRegion *region;
	ZoneMap *zmap;
	int i, idx = ui_ListGetSelectedRow(sky_list);

	if (!regionSettings || !sky_name || idx == 0)
		return;

	eaRemove(&regionSettings->sky_names, idx);
	eaInsert(&regionSettings->sky_names, sky_name, idx-1);

	ui_ListSetSelectedRow(sky_list,idx-1);

	zmap = worldGetActiveMap();
	region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);
	sky_group = StructCreate(parse_SkyInfoGroup);
	for (i = 0; i < eaSize(&regionSettings->sky_names); ++i)
		gfxSkyGroupAddOverride(sky_group, regionSettings->sky_names[i]);
	wleOpSetRegionSkyGroup(region, sky_group);
	StructDestroy(parse_SkyInfoGroup, sky_group);
}

static void wleUIRegionMngrMoveSkyDown(UIButton *button, UIList *sky_list)
{
	WleSettingsRegionMngrRegion *regionSettings = sky_list->pSelectedData;
	char *sky_name = ui_ListGetSelectedObject(sky_list);
	SkyInfoGroup *sky_group;
	WorldRegion *region;
	ZoneMap *zmap;
	int i, idx = ui_ListGetSelectedRow(sky_list);

	if (!regionSettings || !sky_name || idx == eaSize(&regionSettings->sky_names) - 1)
		return;

	eaRemove(&regionSettings->sky_names, idx);
	eaInsert(&regionSettings->sky_names, sky_name, idx+1);

	ui_ListSetSelectedRow(sky_list,idx+1);

	zmap = worldGetActiveMap();
	region = zmapGetWorldRegionByName(zmap, regionSettings->regionName);
	sky_group = StructCreate(parse_SkyInfoGroup);
	for (i = 0; i < eaSize(&regionSettings->sky_names); ++i)
		gfxSkyGroupAddOverride(sky_group, regionSettings->sky_names[i]);
	wleOpSetRegionSkyGroup(region, sky_group);
	StructDestroy(parse_SkyInfoGroup, sky_group);
}

static void wleUIRegionMngrSkyActivated(UIList *list, UserData unused)
{
	const char *skyName = ui_ListGetSelectedObject(list);
	if (skyName)
		emOpenFileEx(skyName, "SkyInfo");
}

// tree drag and drop callback
static void wleUIRegionMngrTreeDrag(UITree *tree, UITreeNode *node, UITreeNode *dragFromParent,
									UITreeNode *dragToParent, int dragToIndex, UserData dragData)
{
	// region drag events
	if (node->displayF == wleUIRegionMngrRegionNodeDisplay)
	{
		// allow reordering
		if (dragToParent == &node->tree->root)
			eaMove(&dragToParent->children, dragToIndex, eaFind(&dragToParent->children, node));
	}
	// layer drag events
	else if (node->displayF == wleUIRegionMngrLayerNodeDisplay)
	{
		// if dragged to a location underneath a region, move the layer to the region
		if (dragToParent->displayF == wleUIRegionMngrRegionNodeDisplay &&
			eaFind(&dragToParent->children, node) == -1)
		{
			ZoneMapLayer *layer = node->contents;
			char *destRegion = ((WleSettingsRegionMngrRegion*) dragToParent->displayData)->regionName;
			const char **layerNames = NULL;

			eaPush(&layerNames, layerGetFilename(layer));
			wleOpSetLayerRegion(layerNames, destRegion);
			eaDestroy(&layerNames);
		}
		// if dragged to root, create a new region
		else if (dragToParent == &node->tree->root)
			wleUINewRegionDialogCreate(layerGetFilename(node->contents));
	}
}

// region editor color window free (to reset modal states)
static void wleUIRegionEditorColorWindowFree(UIColorWindow *window)
{
	ui_WindowSetModal((UIWindow*) window->widget.onFocusData, true);
}

// region editor color button click
static void wleUIRegionEditorColorClicked(UIColorButton *button, UIWindow *modalWin)
{
	ui_WindowHide(modalWin);
	ui_WindowSetModal(modalWin, false);
	ui_WindowShow(modalWin);
	ui_ColorButtonClick(button, NULL);
	button->activeWindow->widget.freeF = wleUIRegionEditorColorWindowFree;
	button->activeWindow->widget.onFocusData = modalWin;
	ui_WindowHide((UIWindow*) button->activeWindow);
	ui_WindowSetModal((UIWindow*) button->activeWindow, true);
	ui_WindowShow((UIWindow*) button->activeWindow);
}

// region editor cancel button click
static bool wleUIRegionEditorCancelClicked(UIWidget *unused, WleUIRegionEditorWin *winWidgets)
{
	WleSettingsRegionMngrRegion ***regions = (WleSettingsRegionMngrRegion***) ui_ListGetModel(winWidgets->overlapList);
	if (regions)
	{
		eaDestroy(regions);
		free(regions);
		ui_ListSetModel(winWidgets->overlapList, NULL, NULL);
	}
	elUIWindowClose(NULL, winWidgets->window);
	free(winWidgets);
	return true;
}

// region editor ok button click
static void wleUIRegionEditorOkClicked(UIButton *button, WleUIRegionEditorWin *winWidgets)
{
	Vec4 color;
	Color c;
	char *oldRegionName = winWidgets->region->regionName;
	const char *newRegionName = ui_TextEntryGetText(winWidgets->regionName);

	// create a new region setting struct if the name changes
	if (strcmpi(newRegionName, oldRegionName) != 0)
	{
		int i;
		const char **layerNames = NULL;

		winWidgets->region = wleUIRegionMngrSettingsRegionFind(newRegionName);
		for (i = 0; i < zmapGetLayerCount(NULL); i++)
		{
			ZoneMapLayer *layer = zmapGetLayer(NULL, i);
			const char *regionName = layerGetWorldRegionString(layer);

			if (!layer)
				continue;

			if (!regionName || strlen(regionName) == 0)
				regionName = "Default";
			if (strcmpi(regionName, oldRegionName) == 0)
				eaPush(&layerNames, layerGetFilename(layer));
		}

		wleOpSetLayerRegion(layerNames, newRegionName);
		eaDestroy(&layerNames);
	}

	// set other region properties
	winWidgets->region->tint = ui_CheckButtonGetState(winWidgets->tint);
	ui_ColorButtonGetColor(winWidgets->color, color);
	color[3] = 0.25;
	vec4ToColor(&c, color);
	winWidgets->region->argb = ARGBFromColor(c);
	wleUIRegionEditorCancelClicked(NULL, winWidgets);
	
	wleUIRegionMngrRefresh();
	EditorPrefStoreStruct(WLE_PREF_EDITOR_NAME, "RegionMngr", zmapGetFilename(NULL), parse_WleSettingsRegionMngr, editorUIState->regionMngrUI.regionMngr);
}

/******
* This function creates a text entry with a combo box that displays the current map's regions and
* allows the user to enter their own region name.
* RETURNS:
*   UITextEntry attached to a region combo box
******/
static UITextEntry *wleUIRegionTextEntryCreate(void)
{
	UIComboBox *cb;
	UITextEntry *entry = NULL;

	wleUIRegionMngrSettingsPurge();
	entry = ui_TextEntryCreate("Default", 0, 0);
	cb = ui_ComboBoxCreate(0, 0, 0, parse_WleSettingsRegionMngrRegion, &editorUIState->regionMngrUI.regionMngr->regions, "regionName");
	ui_TextEntrySetComboBox(entry, cb);

	return entry;
}

/******
* This function creates the new region window, in which the user is prompted to enter a region
* name.
******/
static void wleUINewRegionDialogCreate(const char *layerFilename)
{
	char *labelText = NULL;
	UIWindow *win = ui_WindowCreate("Create new region", 0, 0, 300, 100);
	UILabel *label;
	UITextEntry *entry;

	estrStackCreate(&labelText);
	
	estrPrintf(&labelText, "Specify a new region name for \"%s\".", layerFilename);
	label = ui_LabelCreate(labelText, 5, 5);
	ui_WindowAddChild(win, label);
	win->widget.width = elUINextX(label) + 5;
	label = ui_LabelCreate("Region name:", label->widget.x, elUINextY(label) + 5);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate("", elUINextX(label) + 5, label->widget.y);
	ui_TextEntrySetEnterCallback(entry, wleUINewRegionOkClicked, entry);
	ui_WindowAddChild(win, entry);

	editorUIState->name = strdup(layerFilename);
	editorUIState->currModalWin = win;
	elUIAddCancelOkButtons(win, NULL, NULL, wleUINewRegionOkClicked, entry);
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
	ui_SetFocus(entry);
	estrDestroy(&labelText);
}

/******
* This function creates the region editor window.
* PARAMS:
*   regionSettings - WleSettingsRegionMngrRegion to be edited
******/
static void wleUIRegionEditorCreate(WleSettingsRegionMngrRegion *regionSettings)
{
	Vec4 colorVec;
	Color c;
	Vec3 minVec = {0, 0, 0};
	Vec3 maxVec = {1, 1, 1};
	UIWindow *win = ui_WindowCreate(regionSettings->regionName, 0, 0, 300, 200);
	UILabel *label = ui_LabelCreate("Region name:", 5, 5);
	UITextEntry *entry = ui_TextEntryCreate(regionSettings->regionName, 0, label->widget.y);
	UICheckButton *check = ui_CheckButtonCreate(label->widget.x, elUINextY(label) + 5, "Visible", regionSettings->tint);
	UIList *overlapList = ui_ListCreate(NULL, NULL, 15);
	UIColorButton *color;
	WleUIRegionEditorWin *regionEditorUI = calloc(1, sizeof(WleUIRegionEditorWin));
	WleSettingsRegionMngrRegion ***overlapRegions = calloc(1, sizeof(WleSettingsRegionMngrRegion**));

	const char *regionName = regionSettings->regionName;
	WorldRegion *region;
	Vec3 min, max;
	int i;

	regionEditorUI->region = regionSettings;
	regionEditorUI->window = win;
	ui_WindowAddChild(win, label);
	entry->widget.leftPad = elUINextX(label) + 5;
	entry->widget.rightPad = 5;
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	regionEditorUI->regionName = entry;
	ui_WindowAddChild(win, entry);
	regionEditorUI->tint = check;
	ui_WindowAddChild(win, check);
	color = ui_ColorButtonCreate(elUINextX(check) + 5, check->widget.y, colorVec);
	c = ARGBToColor(regionSettings->argb);
	colorToVec4(colorVec, c);
	ui_ColorButtonSetColor(color, colorVec);
	color->button.clickedF = wleUIRegionEditorColorClicked;
	color->button.clickedData = win;
	color->liveUpdate = true;
	color->noHSV = true;
	color->noAlpha = true;
	regionEditorUI->color = color;
	ui_WindowAddChild(win, color);
	label = ui_LabelCreate("Overlapping regions:", label->widget.x, elUINextY(color) + 10);
	ui_WindowAddChild(win, label);
	overlapList->widget.x = 0;
	overlapList->widget.leftPad = overlapList->widget.rightPad = label->widget.x;
	overlapList->widget.y = elUINextY(label) + 5;
	overlapList->widget.height = 150;
	overlapList->widget.width = 1;
	overlapList->widget.widthUnit = UIUnitPercentage;

	wleUIRegionMngrSettingsPurge();

	// find all overlapping regions and note them
	if (regionName && (strlen(regionName) == 0 || strcmpi(regionName, "Default") == 0))
		regionName = NULL;
	region = zmapGetWorldRegionByName(NULL, regionName);
	if(worldRegionGetBounds(region, min, max))
	{
		for (i = 0; i < eaSize(&editorUIState->regionMngrUI.regionMngr->regions); i++)
		{
			const char *currRegionName = editorUIState->regionMngrUI.regionMngr->regions[i]->regionName;
			WorldRegion *currRegion;
			Vec3 currMin, currMax;

			if (currRegionName && (strlen(currRegionName) == 0 || strcmpi(currRegionName, "Default") == 0))
				currRegionName = NULL;
			currRegion = zmapGetWorldRegionByName(NULL, currRegionName);
			if(worldRegionGetBounds(currRegion, currMin, currMax))
				if (currRegion != region && boxBoxCollision(min, max, currMin, currMax))
					eaPush(overlapRegions, editorUIState->regionMngrUI.regionMngr->regions[i]);
		}
	}

	ui_ListSetModel(overlapList, parse_WleSettingsRegionMngrRegion, overlapRegions);
	ui_ListAppendColumn(overlapList, ui_ListColumnCreateParseName("Region", "regionName", NULL));
	regionEditorUI->overlapList = overlapList;
	ui_WindowAddChild(win, overlapList);
	
	win->widget.height = elUINextY(overlapList) + 40;
	ui_WindowSetModal(win, true);
	elUIAddCancelOkButtons(win, wleUIRegionEditorCancelClicked, regionEditorUI, wleUIRegionEditorOkClicked, regionEditorUI);
	ui_WindowSetCloseCallback(win, wleUIRegionEditorCancelClicked, regionEditorUI);
	elUICenterWindow(win);
	ui_WindowShow(win);
}

/******
* This function refreshes the region manager's main tree.
******/
void wleUIRegionMngrRefresh(void)
{
	UITreeNode *selected = ui_TreeGetSelected(editorUIState->regionMngrUI.tree);
	void *displayData = selected ? selected->displayData : NULL;
	ui_TreeRefresh(editorUIState->regionMngrUI.tree);
	wleUIRegionMngrTreeSetWidth();

	if (!displayData)
		ui_TreeUnselectAll(editorUIState->regionMngrUI.tree);

	wleUIRegionMngrTreeSelected(editorUIState->regionMngrUI.tree, editorUIState->regionMngrUI.sky_list);
}

/******
* This function draws the region boxes according to the user's settings.
******/
static void wleUIRegionMngrDraw(void)
{
	int i;

	// draw the region boxes
	for (i = 0; i < eaSize(&editorUIState->regionMngrUI.regionMngr->regions); i++)
	{
		char *regionName = editorUIState->regionMngrUI.regionMngr->regions[i]->regionName;
		WorldRegion *region = zmapGetWorldRegionByName(NULL, regionName && strcmpi(regionName, "Default") == 0 ? NULL: regionName);
		Vec3 min, max;

		if (region && editorUIState->regionMngrUI.regionMngr->regions[i]->tint)
		{
			if(worldRegionGetBounds(region, min, max))
				gfxDrawBox3DARGB(min, max, unitmat, editorUIState->regionMngrUI.regionMngr->regions[i]->argb, 0);
		}
	}
}

/********************
* FILE LIST
********************/
void wleUIFileListRefresh(void)
{
	int i;
	EMEditorDoc *doc = wleGetWorldEditorDoc();
	const char *zmapFilename;

	if (!doc)
		return;

	zmapFilename = zmapGetFilename(NULL);

	emDocRemoveAllFiles(doc, false);
	if (zmapFilename) {
		int layer_cnt = zmapGetLayerCount(NULL);

		//Zone Map
		emDocAssocFile(doc, zmapFilename);

		//Layers
		for ( i=0; i < layer_cnt; i++ ) {
			ZoneMapLayer *layer = zmapGetLayer(NULL, i);
			emDocAssocFile(doc, layerGetFilename(layer));
		}
	}
}

/********************
* VIEW CONFIRM DIALOG
********************/
typedef struct WleUIViewConfirmWin
{
	UIWindow *win;
	ZoneMapLayer *layer;
} WleUIViewConfirmWin;

static bool wleUIViewConfirmCancelClicked(UIWidget *unused, WleUIViewConfirmWin *confirmUI)
{
	elUIWindowClose(NULL, confirmUI->win);
	free(confirmUI);
	return true;
}

static void wleUIViewConfirmViewClicked(UIButton *button, WleUIViewConfirmWin *confirmUI)
{
	wleUISetLayerMode(confirmUI->layer, LAYER_MODE_GROUPTREE, false);
	wleUIViewConfirmCancelClicked(NULL, confirmUI);
}

static void wleUIViewConfirmLockClicked(UIButton *button, WleUIViewConfirmWin *confirmUI)
{
	wleUISetLayerMode(confirmUI->layer, LAYER_MODE_EDITABLE, false);
	wleUIViewConfirmCancelClicked(NULL, confirmUI);
}

void wleUIViewConfirmDialogCreate(ZoneMapLayer *layer)
{
	char text[MAX_PATH + 1024];
	UIWindow *win;
	UILabel *label;
	UIButton *button;
	int xMax;
	WleUIViewConfirmWin *confirmUI = calloc(1, sizeof(WleUIViewConfirmWin));
	
	if (!layer || layerGetTracker(layer))
		return;

	confirmUI->layer = layer;
	sprintf(text, "View \"%s\"?", layerGetFilename(layer));
	win = ui_WindowCreate(text, 0, 0, 100, 100);
	sprintf(text, "This object belongs to \"%s\", which is not yet viewable.", layerGetFilename(layer));
	label = ui_LabelCreate(text, 5, 5);
	xMax = elUINextX(label) + 5;
	ui_WindowAddChild(win, label);
	sprintf(text, "Do you wish to view this layer?");
	label = ui_LabelCreate(text, label->widget.x, elUINextY(label) + 5);
	ui_WindowAddChild(win, label);
	button = ui_ButtonCreate("Lock", 5, 5, wleUIViewConfirmLockClicked, confirmUI);
	button->widget.offsetFrom = UIBottomRight;
	ui_WindowAddChild(win, button);
	button = ui_ButtonCreate("View", elUINextX(button) + 5, 5, wleUIViewConfirmViewClicked, confirmUI);
	button->widget.offsetFrom = UIBottomRight;
	ui_WindowAddChild(win, button);
	button = ui_ButtonCreate("Cancel", elUINextX(button) + 5, 5, wleUIViewConfirmCancelClicked, confirmUI);
	button->widget.offsetFrom = UIBottomRight;
	ui_WindowAddChild(win, button);

	confirmUI->win = win;
	win->widget.width = xMax;
	win->widget.height = elUINextY(button) + elUINextY(label) + 5;
	ui_WindowSetCloseCallback(win, wleUIViewConfirmCancelClicked, confirmUI);
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
}

/********************
* MAP PROPERTIES PANEL
********************/
static void wleUIMapPropertiesDisplayNameChanged(UIMessageEntry *entry, UserData unused)
{
	wleOpSetMapDisplayName(ui_MessageEntryGetMessage(entry));
	wleUIMapPropertiesRefresh();
}

static void wleUIMapPropertiesMapTypeChanged(UIComboBox *combo, int type, UserData unused)
{
	ZoneMapType mapType = type;
	if (mapType != -1)
		wleOpSetMapType(mapType);
	else
		emStatusPrintf("Invalid map type.");
	wleUIMapPropertiesRefresh();
}

static void wleUIMapPropertiesRespawnTypeChanged(UIComboBox *combo, int type, UserData unused)
{
	ZoneRespawnType eRespawnType = type;
	if(eRespawnType != -1)
		wleOpSetMapRespawnType(eRespawnType);
	else
		emStatusPrintf("Invalid respawn type.");
	wleUIMapPropertiesRefresh();
}

static void wleUIMapPropertiesRespawnWaveTimeChanged(UITextEntry *entry, UserData unused)
{
	const char *pchValueString = ui_TextEntryGetText(entry);
	U32 value = atoi(pchValueString);

	wleOpSetMapRespawnWaveTime(value);
}

static void wleUIMapPropertiesRespawnMinTimeChanged(UITextEntry *entry, UserData unused)
{
	const char *pchValueString = ui_TextEntryGetText(entry);
	U32 value = atoi(pchValueString);

	wleOpSetMapRespawnMinTime(value);
}

static void wleUIMapPropertiesRespawnMaxTimeChanged(UITextEntry *entry, UserData unused)
{
	const char *pchValueString = ui_TextEntryGetText(entry);
	U32 value = atoi(pchValueString);

	wleOpSetMapRespawnMaxTime(value);
}

static void wleUIMapPropertiesRespawnIncrTimeChanged(UITextEntry *entry, UserData unused)
{
	const char *pchValueString = ui_TextEntryGetText(entry);
	U32 value = atoi(pchValueString);

	wleOpSetMapRespawnIncrementTime(value);
}

static void wleUIMapPropertiesRespawnAttrTimeChanged(UITextEntry *entry, UserData unused)
{
	const char *pchValueString = ui_TextEntryGetText(entry);
	U32 value = atoi(pchValueString);

	wleOpSetMapRespawnAttritionTime(value);
}

static void welUIMapPropertiesDefaultQueueDefChanged(UITextEntry *entry, UserData unused)
{
	wleUISetDefaultQueueDef(ui_TextEntryGetText(entry));
}

static void wleUIMapPropertiesDefaultPVPGameTypeChanged(UIComboBox *combo, int iGameType, UserData unused)
{
	wleUISetPVPGameType(StaticDefineIntRevLookup(PVPGameTypeEnum,iGameType));
}

static void wleUIMapPropertiesPublicNameChanged(UITextEntry *entry, UserData unused)
{
	const char *publicName = ui_TextEntryGetText(entry);
	bool isValid = true;

	if (!strchr(publicName, ' ') && !strchr(publicName, ','))
	{
		if (strlen(publicName))
		{
			const char *filename = zmapGetFilename(NULL);
			if (filename)
			{
				ANALYSIS_ASSUME(filename);
				if (strstri(filename, "/_"))
				{
					emStatusPrintf("Private maps (in underscore folders) are not allowed to have a public name.");
					isValid = false;
				}
			}
			if (isValid && worldGetZoneMapByPublicName(publicName))
			{
				emStatusPrintf("That public name is already in use.");
				isValid = false;
			}
		}
	}
	else
	{
		emStatusPrintf("Invalid public name.");
		isValid = false;
	}

	if (isValid)
		wleOpSetPublicName(publicName);
	wleUIMapPropertiesRefresh();
}

static void wleUIMapPropertiesLevelChanged(UITextEntry *entry, UserData unused)
{
	const char *pchLevelString = ui_TextEntryGetText(entry);
	U32 value = atoi(pchLevelString);
	wleOpSetMapLevel(value);
	wleUIMapPropertiesRefresh();
}

static void wleUIMapPropertiesDifficultyChanged(UITextEntry *entry, UserData unused)
{
	const char *pchDifficultyString = ui_TextEntryGetText(entry);
	EncounterDifficulty eDifficulty = pchDifficultyString ? StaticDefineIntGetInt(EncounterDifficultyEnum, pchDifficultyString) : 0;
	wleOpSetMapDifficulty(eDifficulty);
	g_eEditorDifficulty = eDifficulty;
	wleUIMapPropertiesRefresh();
}

EncounterDifficulty wleGetEncounterDifficulty(WorldEncounterProperties *pEncounter)
{
	EncounterDifficulty eDifficulty = g_eEditorDifficulty;
	EncounterTemplate* pTemplate = pEncounter ? GET_REF(pEncounter->hTemplate) : NULL;
	EncounterDifficultyProperties* pDifficultyProps = pTemplate ? pTemplate->pDifficultyProperties : NULL;
	WorldEncounterSpawnProperties* pSpawnProps = pEncounter ? pEncounter->pSpawnProperties : NULL;

	// Get difficulty
	if(pDifficultyProps)
	{
		switch(pDifficultyProps->eDifficultyType)
		{
			case EncounterDifficultyType_MapDifficulty:
			{
				eDifficulty = g_eEditorDifficulty;
			}
			xcase EncounterDifficultyType_Specified:
			{
				eDifficulty =  pDifficultyProps->eSpecifiedDifficulty;
			}
			xcase EncounterDifficultyType_MapVariable:
			{
				if(pDifficultyProps->pcMapVariable)
				{
					WorldVariableDef *pDef = zmapInfoGetVariableDefByName(NULL, pDifficultyProps->pcMapVariable);
					WorldVariable *pVar = pDef ? wleAEWorldVariableCalcVariableNonRandom(pDef) : NULL;

					if(pVar)
					{
						if(pVar->eType == WVAR_INT)
						{
							return pVar->iIntVal;
						} 
						else if(pVar->eType == WVAR_STRING && pVar->pcStringVal)
						{
							EncounterDifficulty eVarDifficulty = StaticDefineIntGetInt(EncounterDifficultyEnum, pVar->pcStringVal);
							if(eVarDifficulty > 0)
								eDifficulty =  eVarDifficulty;
						}

					}
				}
			}
		}
	}

	// Apply offset
	if(pSpawnProps)
	{
		eDifficulty += pSpawnProps->iDifficultyOffset;
	}

	return eDifficulty;
}


static void wleUIMapPropertiesForceTeamSizeChanged(UITextEntry *entry, UserData unused)
{
	const char *pchTeamSizeString = ui_TextEntryGetText(entry);
	U32 value = CLAMP(atoi(pchTeamSizeString),0,5);
	wleOpSetMapForceTeamSize(value);
	wleUIMapPropertiesRefresh();
}

static void wleUIMapIgnoreTeamSizeBonusXPChanged(UICheckButton *button, UserData unused)
{
	wleOpSetMapIgnoreTeamSizeBonusXP(ui_CheckButtonGetState(button));
	wleUIMapPropertiesRefresh();
}

static void wleUIMapUsedInUGCChanged(UICheckButton *button, UserData unused)
{
	wleOpSetMapUsedInUGC(ui_CheckButtonGetState(button));
	wleUIMapPropertiesRefresh();
}

static void wleUIMapPropertiesPrivacyChanged(UITextEntry *entry, UserData unused)
{
	wleOpSetMapPrivacy(ui_TextEntryGetText(entry));
}

static void wleUIParentMapNameChanged(UITextEntry *entry, UserData unused)
{
	wleOpSetMapParentMapName(ui_TextEntryGetText(entry));
}

static void wleUIParentMapSpawnChanged(UITextEntry *entry, UserData unused)
{
	wleOpSetMapParentMapSpawn(ui_TextEntryGetText(entry));
}

static void wleUIStartSpawnNameChanged(UITextEntry *entry, UserData unused)
{
	wleOpSetMapStartSpawn(ui_TextEntryGetText(entry));
}

static void wleUIRewardTableChanged(UIComboBox *combo, UserData unused)
{
	ResourceInfo *info = ui_ComboBoxGetSelectedObject(combo);
	const char *rewardTableKey = info ? info->resourceName : NULL;
	wleOpSetMapRewardTable(rewardTableKey);
}

static void wleUIPlayerRewardTableChanged(UITextEntry *entry, UserData unused)
{
	const char *rewardTableKey = ui_TextEntryGetText(entry);
	wleOpSetMapPlayerRewardTable(rewardTableKey);
}

static void wleUIRequiresExprChanged(UIExpressionEntry *entry, UserData unused)
{
	const char *pchExprText = ui_ExpressionEntryGetText(entry);
	Expression *pExpr = exprCreateFromString(pchExprText, zmapGetFilename(NULL));
	wleOpSetMapRequiresExpr(pExpr);
}

static void wleUIPermissionExprChanged(UIExpressionEntry *entry, UserData unused)
{
	const char *pchExprText = ui_ExpressionEntryGetText(entry);
	Expression *pExpr = exprCreateFromString(pchExprText, zmapGetFilename(NULL));
	wleOpSetMapPermissionExpr(pExpr);
}

static void wleUIRequiredClassCategorySetComboChanged(UITextEntry *entry, UserData unused)
{
	wleOpSetMapRequiredClassCategorySet(ui_TextEntryGetText(entry));
}

static void wleUIMastermindDefChanged(UITextEntry *entry, UserData unused)
{
	wleOpSetMapMastermindDef(ui_TextEntryGetText(entry));
}

static void wleUICivilianMapDefChanged(UITextEntry *entry, UserData unused)
{
	wleOpSetMapCivilianMapDef(ui_TextEntryGetText(entry));
}

static void wleUIVisitedTrackerToggled(UICheckButton *button, UserData unused)
{
	wleOpSetMapDisableVisitedTracking(ui_CheckButtonGetState(button));
}

void wleUIMapPropertiesRefresh(void)
{
	ZoneMapInfo *zminfo = zmapGetInfo(worldGetActiveMap());
	ZoneMapType mapType = zmapInfoGetMapType(zminfo);
	const char *publicName = zmapInfoGetCurrentName(zminfo);
	DisplayMessage *message = zmapInfoGetDisplayNameMessage(zminfo);
	Message *zmapMessage = message ? (message->pEditorCopy ? message->pEditorCopy : GET_REF(message->hMessage)) : NULL;
	Message *messageCopy = StructClone(parse_Message, zmapMessage);
	const char *zmapFilename = zmapInfoGetFilename(zminfo);
	U32 level = zmapInfoGetMapLevel(zminfo);
	U32 force_team_size = zmapInfoGetMapForceTeamSize(zminfo);
	bool bIgnoreTeamSizeBonusXP = zmapInfoGetMapIgnoreTeamSizeBonusXP(zminfo);
	bool bUsedInUGC = zmapInfoGetUsedInUGC(zminfo);
	bool bMultipleDifficulties = (encounter_GetEncounterDifficultiesCount() > 1);
	S32 difficulty = bMultipleDifficulties ? zmapInfoGetMapDifficulty(zminfo) : 0;
	const char **privateUsers = zmapInfoGetPrivacy(zminfo);
	ZoneRespawnType eRespawnType = zmapInfoGetRespawnType(zminfo);
	U32 respawnWaveTime = zmapInfoGetRespawnWaveTime(zminfo);
	U32 respawnMinTime, respawnMaxTime, respawnIncrTime, respawnAttrTime; 

	bool privateMap = false;
	char entryBuffer[1024];
	int i;
	if (zmapFilename)
	{
		ANALYSIS_ASSUME(zmapFilename);
		privateMap = (strstri(zmapFilename, "/_") != 0);
	}

	zmapInfoGetRespawnTimes(zminfo, &respawnMinTime, &respawnMaxTime, &respawnIncrTime, &respawnAttrTime);

	// set defaults on message
	if (!messageCopy)
		messageCopy = StructCreate(parse_Message);
	assert(messageCopy);
	if (!messageCopy->pcMessageKey)
		messageCopy->pcMessageKey = zmapInfoGetDefaultDisplayNameMsgKey(zminfo);
	if (!messageCopy->pcScope)
		messageCopy->pcScope = zmapInfoGetDefaultMessageScope(NULL);

	ui_ComboBoxSetSelectedEnum(editorUIState->mapPropertiesUI.mapTypeCombo, mapType);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.publicNameEntry, publicName ? publicName : "");
	ui_MessageEntrySetMessage(editorUIState->mapPropertiesUI.displayNameEntry, messageCopy);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.DefaultQueueEntry, zmapInfoGetDefaultQueueDef(zminfo));
	ui_ComboBoxSetSelectedEnum(editorUIState->mapPropertiesUI.DefaultPVPGameTypeCombo, StaticDefineIntGetInt(PVPGameTypeEnum,zmapInfoGetDefaultPVPGameType(zminfo)));
	sprintf(entryBuffer, "%d", level);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.mapLevelEntry, entryBuffer);
	if(bMultipleDifficulties)
		ui_TextEntrySetText(editorUIState->mapPropertiesUI.mapDifficultyEntry, StaticDefineIntRevLookupNonNull(EncounterDifficultyEnum, difficulty));
	sprintf(entryBuffer, "%d", force_team_size);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.mapForceTeamSizeEntry, entryBuffer);
	ui_CheckButtonSetState(editorUIState->mapPropertiesUI.ignoreTeamSizeBonusXPButton, bIgnoreTeamSizeBonusXP);
	ui_CheckButtonSetState(editorUIState->mapPropertiesUI.usedInUgcButton, bUsedInUGC);
	ui_ComboBoxSetSelectedEnum(editorUIState->mapPropertiesUI.respawnTypeCombo,eRespawnType);

	sprintf(entryBuffer, "%d", respawnWaveTime);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.respawnWaveTimeEntry, entryBuffer);
	sprintf(entryBuffer, "%d", respawnMinTime);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.respawnMinTimeEntry, entryBuffer);
	sprintf(entryBuffer, "%d", respawnMaxTime);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.respawnMaxTimeEntry, entryBuffer);
	sprintf(entryBuffer, "%d", respawnIncrTime);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.respawnIncrTimeEntry, entryBuffer);
	sprintf(entryBuffer, "%d", respawnAttrTime);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.respawnAttrTimeEntry, entryBuffer);

	entryBuffer[0] = '\0';
	for (i = 0; i < eaSize(&privateUsers) - 1; i++)
		strcatf(entryBuffer, "%s,", privateUsers[i]);
	if (privateUsers)
		strcatf(entryBuffer, "%s", privateUsers[i]);
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.privateToEntry, entryBuffer);

	ui_TextEntrySetText(editorUIState->mapPropertiesUI.parentMapEntry, zmapInfoGetParentMapName(zminfo));
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.parentMapSpawnEntry, zmapInfoGetParentMapSpawnPoint(zminfo));
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.startSpawnEntry, zmapInfoGetStartSpawnName(zminfo));
	{
		const char *rewardTableKey = zmapInfoGetRewardTableString(zminfo);
		ResourceInfo *resInfo = rewardTableKey ? resGetInfo("RewardTable", rewardTableKey) : NULL;
		ui_ComboBoxSetSelectedObject(editorUIState->mapPropertiesUI.rewardTableCombo, resInfo);
	}
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.playerRewardTableEntry,zmapInfoGetPlayerRewardTableString(zminfo));
	ui_ExpressionEntrySetText(editorUIState->mapPropertiesUI.requiresExprEntry, exprGetCompleteString(zmapInfoGetRequiresExpr(zminfo)));
	ui_ExpressionEntrySetText(editorUIState->mapPropertiesUI.permissionExprEntry, exprGetCompleteString(zmapInfoGetPermissionExpr(zminfo)));
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.requiredClassCategorySetEntry, zmapInfoGetRequiredClassCategorySetString(zminfo));
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.mastermindDefEntry, zmapInfoGetMastermindDefKey(zminfo));
	ui_TextEntrySetText(editorUIState->mapPropertiesUI.civilianMapDefEntry, zmapInfoGetCivilianMapDefKey(zminfo));
	ui_CheckButtonSetState(editorUIState->mapPropertiesUI.disableVisitedTrackingButton, zmapInfoGetDisableVisitedTracking(zminfo));

	ui_SetActive(UI_WIDGET(editorUIState->mapPropertiesUI.publicNameEntry), !privateMap);
	ui_SetActive(UI_WIDGET(editorUIState->mapPropertiesUI.displayNameEntry), !privateMap);
	ui_SetActive(UI_WIDGET(editorUIState->mapPropertiesUI.parentMapEntry), (mapType == ZMTYPE_STATIC));
	ui_SetActive(UI_WIDGET(editorUIState->mapPropertiesUI.parentMapSpawnEntry), (mapType == ZMTYPE_STATIC));

	StructDestroy(parse_Message, messageCopy);
}

void wleUIMiscPropertiesRefresh(void)
{
	ZoneMapInfo *zminfo = zmapGetInfo(worldGetActiveMap());
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.collectDoorStatusCheck, zmapInfoGetCollectDoorDestStatus(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.shardVariablesCheck, zmapInfoGetEnableShardVariables(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.duelsCheck, zmapInfoGetDisableDuels(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.powersRequireValidTargetCheck, zmapInfoGetPowersRequireValidTarget(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.unteamedOwnedMapCheck, zmapInfoGetTeamNotRequired(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.guildOwnedCheck, zmapInfoGetIsGuildOwned(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.guildNotRequiredCheck, zmapInfoGetGuildNotRequired(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.disableInstanceChangeCheck, zmapInfoGetDisableInstanceChanging(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.recordPlayerMatchStats, zmapInfoGetRecordPlayerMatchStats(zminfo));
	ui_CheckButtonSetState(editorUIState->miscPropertiesUI.enableUpsellFeatures, zmapInfoGetEnableUpsellFeatures(zminfo));
}


/********************
* GAE LAYERS PANEL
********************/
static void wleUIAddGAELayer(UIButton *button, void *unused)
{
	GlobalGAELayerDef *def;
	char *buf = NULL;

	if(ui_ComboBoxGetSelected(editorUIState->globalGAELayersUI.selectedGAELayer) >= 0)
	{
		ui_ComboBoxGetSelectedsAsString(editorUIState->globalGAELayersUI.selectedGAELayer, &buf);

		if(buf)
		{
			// Create the def
			def = StructCreate(parse_GlobalGAELayerDef);
			def->name = allocAddString(buf);
			
			// actually 'add' the layer
			//
			wleOpAddGlobalGAELayer(def);

			StructDestroy(parse_GlobalGAELayerDef, def);
		}
		estrDestroy(&buf);
	}
}

static void wleUIGlobalGAELayersEntryFree(GlobalGAELayersEntry *entry)
{
	ui_WidgetQueueFreeAndNull(&entry->removeButton);
	ui_WidgetQueueFreeAndNull(&entry->gaeLayerCombo);
	
	free(entry);
}

void wleUISelectGAELayer(UIAnyWidget *ui_widget, UserData user_data) 
{
	GlobalGAELayersEntry *entry = (GlobalGAELayersEntry*)user_data;
	GlobalGAELayerDef *def;
	GlobalGAELayerDef *new_def;
	char *buf = NULL;

	if(ui_ComboBoxGetSelected(entry->gaeLayerCombo) >= 0)
	{
		ui_ComboBoxGetSelectedsAsString(entry->gaeLayerCombo, &buf);
		if(buf)
		{
			def = zmapInfoGetGAELayerDef(NULL, entry->index);
			new_def = StructClone(parse_GlobalGAELayerDef, def);
			assert(new_def);

			new_def->name = allocAddString(buf);

			wleOpModifyGlobalGAELayer(entry->index, new_def);

			StructDestroy(parse_GlobalGAELayerDef, new_def);
		}
		estrDestroy(&buf);
	}
}

void wleUIRemoveGAELayer(UIAnyWidget *ui_widget, UserData user_data) 
{
	GlobalGAELayersEntry *entry = (GlobalGAELayersEntry*)user_data;
	wleOpRemoveGlobalGAELayer(entry->index);
}


static F32 wleUIGlobalGAELayersRefreshEntry(GlobalGAELayerDef *var_def, int index, GlobalGAELayersEntry *entry, F32 y)
{
	EMPanel *panel = editorUIState->globalGAELayersUI.globalGAELayersPanel;
	
	entry->index = index;

	if (!entry->removeButton)
	{
		// add the remove button
		entry->removeButton = ui_ButtonCreate("Remove", 0, y, wleUIRemoveGAELayer, entry);
		emPanelAddChild(panel, entry->removeButton, false);
	}

	if(!entry->gaeLayerCombo) 
	{
		// Add the combo-box
		entry->gaeLayerCombo = (UIComboBox*)ui_FilteredComboBoxCreate(elUINextX(entry->removeButton)+5, y, 200, NULL, &editorUIState->globalGAELayersUI.gaeLayerChoices, NULL);
		
		ui_WidgetSetWidthEx(UI_WIDGET(entry->gaeLayerCombo), 1.0, UIUnitPercentage);
		ui_ComboBoxSetSelectedCallback(entry->gaeLayerCombo, wleUISelectGAELayer, entry);

		emPanelAddChild(panel, entry->gaeLayerCombo, false);
	}
	ui_ComboBoxSetSelectedsAsString(entry->gaeLayerCombo, var_def->name);
	

	// next-line (y-offset)
	y = elUINextY(entry->removeButton) + 5;

	return y;
}


void wleUIReloadGlobalGAELayersCB(const char *relPath, int when, void *userData)
{
	wleUIReloadGlobalGAELayers();
}

void wleUIReloadGlobalGAELayers(void) 
{
	int i;
	DictionaryEArrayStruct *eaStruct;

	eaStruct = resDictGetEArrayStruct(g_GAEMapDict);
	if(eaStruct)
	{
		if(editorUIState->globalGAELayersUI.gaeLayerChoices)
		{
			eaDestroy(&editorUIState->globalGAELayersUI.gaeLayerChoices);
		}

		// allocate eArray
		eaCreate(&editorUIState->globalGAELayersUI.gaeLayerChoices);

		// extract the names and push
		for(i = 0; i < eaSize(&eaStruct->ppReferents); i++)
		{
			GameAudioEventMap *map = eaStruct->ppReferents[i];

			// do not add it if it matches _the_ global layer
			// it will be loaded on all maps regardless (no need to select what is already loaded)
			if(strcmpi(map->filename, GLOBAL_GAE_LAYER_FILENAME))
			{
				eaPush(&editorUIState->globalGAELayersUI.gaeLayerChoices, map->filename);
			}
		}
	}
}

void wleUISetupGlobalGAELayers(void) 
{
	// make sure the GAE Layer map dictionary is init'd
	if(!g_GAEMapDict)
	{
		sndCommonCreateGAEDict();
	}

	// determine all GAE Layer choices for combo box(es) (if necessary)
	if(!editorUIState->globalGAELayersUI.gaeLayerChoices)
	{
		wleUIReloadGlobalGAELayers();
	}
}

void wleUIGlobalGAELayersRefresh(void)
{
	F32 y = 0;
	int numVars = zmapInfoGetGAELayersCount(NULL);
	int i;

	// iterate through all global gae layers from the zone map and setup their entry properties
	for(i=0; i<numVars; ++i) 
	{
		GlobalGAELayerDef *varDef = zmapInfoGetGAELayerDef(NULL, i);
		if (i >= eaSize(&editorUIState->globalGAELayersUI.entries))
		{
			GlobalGAELayersEntry *entry = calloc(1, sizeof(GlobalGAELayersEntry));
			eaPush(&editorUIState->globalGAELayersUI.entries, entry);
		}

		assert(editorUIState->globalGAELayersUI.entries);
		y = wleUIGlobalGAELayersRefreshEntry(varDef, i, editorUIState->globalGAELayersUI.entries[i], y);
	}

	// free and remove all remaining entries
	for(; i<eaSize(&editorUIState->globalGAELayersUI.entries); )
	{
		wleUIGlobalGAELayersEntryFree(editorUIState->globalGAELayersUI.entries[i]);
		eaRemove(&editorUIState->globalGAELayersUI.entries, i);
	}

	// add an 'add gae layer' button if necessary
	if(!editorUIState->globalGAELayersUI.addButton)
	{
		editorUIState->globalGAELayersUI.addButton = ui_ButtonCreate("Add GAE Layer", 0, y, wleUIAddGAELayer, NULL);
		ui_WidgetSetWidth(UI_WIDGET(editorUIState->globalGAELayersUI.addButton), 100);
		emPanelAddChild(editorUIState->globalGAELayersUI.globalGAELayersPanel, editorUIState->globalGAELayersUI.addButton, true);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(editorUIState->globalGAELayersUI.addButton), 0, y, 0, 0, UITopRight);

	// Add the combo-box layer choice to add
	if(!editorUIState->globalGAELayersUI.selectedGAELayer)
	{
		editorUIState->globalGAELayersUI.selectedGAELayer = (UIComboBox*)ui_FilteredComboBoxCreate(0, y, 200, NULL, &editorUIState->globalGAELayersUI.gaeLayerChoices, NULL);
		ui_WidgetSetWidthEx(UI_WIDGET(editorUIState->globalGAELayersUI.selectedGAELayer), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(editorUIState->globalGAELayersUI.selectedGAELayer), 0, ui_WidgetGetWidth(UI_WIDGET(editorUIState->globalGAELayersUI.addButton))+5, 0, 0);
		ui_ComboBoxSetSelectedCallback(editorUIState->globalGAELayersUI.selectedGAELayer, NULL, NULL);

		emPanelAddChild(editorUIState->globalGAELayersUI.globalGAELayersPanel, editorUIState->globalGAELayersUI.selectedGAELayer, false);
	} 
	ui_WidgetSetPositionEx(UI_WIDGET(editorUIState->globalGAELayersUI.selectedGAELayer), 0, y, 0, 0, UITopLeft);
	
	

	// update size of panel to accommodate new controls
	emPanelUpdateHeight(editorUIState->globalGAELayersUI.globalGAELayersPanel);
}


/********************
* VARIABLE PROPERTIES PANEL
********************/

static void wleUIAddVariable(UIButton *button, void *unused)
{
	WorldVariableDef *def;
	char buf[128];
	int i;
	int count = 1;

	// Find a unique name
	while (true)
	{
		if (count == 1)
			strcpy(buf, "New_Variable");
		else
			sprintf(buf, "New_Variable_%d", count);

		for(i=zmapInfoGetVariableCount(NULL)-1; i>=0; --i)
		{
			WorldVariableDef *def2 = zmapInfoGetVariableDef(NULL, i);
			if (def2 && (stricmp(def2->pcName, buf) == 0))
				break;
		}
		if (i < 0)
			break;

		++count;
	}

	// Create the def
	def = StructCreate(parse_WorldVariableDef);
	def->pcName = allocAddString(buf);
	def->eType = WVAR_INT;
	def->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
	def->pSpecificValue = StructCreate(parse_WorldVariable);
	def->pSpecificValue->eType = WVAR_INT;

	wleOpAddVariable(def);

	StructDestroy(parse_WorldVariableDef, def);
}

static void wleUIRemoveVariable(UIButton *button, VariableDefPropertiesEntry *entry)
{
	wleOpRemoveVariable(entry->index);
}

static void wleUIVariableDefPropertiesFree(VariableDefPropertiesEntry *entry)
{
	ui_WidgetQueueFreeAndNull(&entry->nameLabel);
	ui_WidgetQueueFreeAndNull(&entry->typeLabel);
	ui_WidgetQueueFreeAndNull(&entry->defaultLabel);
	ui_WidgetQueueFreeAndNull(&entry->valueLabel);
	ui_WidgetQueueFreeAndNull(&entry->choiceTableLabel);
	ui_WidgetQueueFreeAndNull(&entry->choiceNameLabel);
	ui_WidgetQueueFreeAndNull(&entry->choiceIndexLabel);
	ui_WidgetQueueFreeAndNull(&entry->expressionLabel);
	ui_WidgetQueueFreeAndNull(&entry->removeButton);
	ui_WidgetQueueFreeAndNull(&entry->nameEntry);
	ui_WidgetQueueFreeAndNull(&entry->typeCombo);
	ui_WidgetQueueFreeAndNull(&entry->defaultValueCombo);
	ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
	ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
	ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
	ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
	ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
	ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
	ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
	ui_WidgetQueueFreeAndNull(&entry->varChoiceTableValue);
	ui_WidgetQueueFreeAndNull(&entry->varChoiceNameValue);
	ui_WidgetQueueFreeAndNull(&entry->varChoiceIndexValue);
	ui_WidgetQueueFreeAndNull(&entry->varExpressionValue);
	ui_WidgetQueueFreeAndNull(&entry->isPublicButton);

	eaDestroyEx(&entry->choiceTableNames, NULL);
	free(entry);
}

static void wleUIVariableNameChanged(UITextEntry *text_entry, VariableDefPropertiesEntry *entry)
{
	WorldVariableDef *def = zmapInfoGetVariableDef(NULL, entry->index);
	WorldVariableDef *new_def = StructClone(parse_WorldVariableDef, def);
	assert(new_def);

	new_def->pcName = allocAddString(ui_TextEntryGetText(text_entry));

	wleOpModifyVariable(entry->index, new_def);

	StructDestroy(parse_WorldVariableDef, new_def);
}

static void wleUIVariableTypeChanged(UIComboBox *combo, int type, VariableDefPropertiesEntry *entry)
{
	WorldVariableDef *def = zmapInfoGetVariableDef(NULL, entry->index);

	if (def && (type != def->eType)) {
		WorldVariableDef *new_def = StructClone(parse_WorldVariableDef, def);
		assert(new_def);

		new_def->eType = (WorldVariableType)type;

		// Clean up based on the old type
		if (new_def->pSpecificValue)
		{
			new_def->pSpecificValue->eType = new_def->eType;
			worldVariableCleanup(new_def->pSpecificValue);
		}

		if (new_def->activity_default_value)
		{
			new_def->activity_default_value->eType = new_def->eType;
			worldVariableCleanup(new_def->activity_default_value);
		}

		wleOpModifyVariable(entry->index, new_def);

		StructDestroy(parse_WorldVariableDef, new_def);
	}
}

static void wleUIVariableDefaultTypeChanged(UIComboBox *combo, int default_type, VariableDefPropertiesEntry *entry)
{
	WorldVariableDef *def = zmapInfoGetVariableDef(NULL, entry->index);
	WorldVariableDef *new_def = StructClone(parse_WorldVariableDef, def);
	assert(new_def);

	new_def->eDefaultType = (WorldVariableDefaultValueType)default_type;

	wleOpModifyVariable(entry->index, new_def);

	StructDestroy(parse_WorldVariableDef, new_def);
}

static void wleUIVariableTextValueForWorldVarChanged( WorldVariableDef * def, WorldVariable* var, const char * text, UIWidget *text_entry, VariableDefPropertiesEntry *entry)
{
	UITextEntry* pEntry = (UITextEntry*) text_entry;
	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch(def->eType)
	{
		xcase WVAR_INT:
			var->iIntVal = atoi(text);
		xcase WVAR_FLOAT:
			var->fFloatVal = atof(text);
		xcase WVAR_LOCATION_STRING: 
		case WVAR_STRING:
		case WVAR_ANIMATION: 
		case WVAR_ITEM_DEF: 
		case WVAR_MISSION_DEF:
			StructFreeString(var->pcStringVal);
			var->pcStringVal = StructAllocString(text);
		xcase WVAR_CRITTER_DEF:
			if (text) 
				SET_HANDLE_FROM_STRING("CritterDef", text, var->hCritterDef);
			else
				REMOVE_HANDLE(var->hCritterDef);
		xcase WVAR_CRITTER_GROUP:
			if (text) 
				SET_HANDLE_FROM_STRING("CritterGroup", text, var->hCritterGroup);
			else
				REMOVE_HANDLE(var->hCritterGroup);
		xcase WVAR_MAP_POINT:
			if (pEntry == entry->varMapNameValue)
			{
				StructFreeString(var->pcZoneMap);
				var->pcZoneMap = StructAllocString(text);
			}
			else if (pEntry == entry->varSimpleValue)
			{
				StructFreeString(var->pcStringVal);
				var->pcStringVal = StructAllocString(text);
			}
		xdefault:
		assertmsg(0, "Unexpected type in variable value text change callback");
	}
}
static void wleUIVariableTextValueChanged(UIWidget *text_entry, VariableDefPropertiesEntry *entry)
{
	WorldVariableDef *def = zmapInfoGetVariableDef(NULL, entry->index);
	WorldVariableDef *new_def = StructClone(parse_WorldVariableDef, def);
	assert(new_def);

	if( new_def->eDefaultType == WVARDEF_SPECIFY_DEFAULT ) {
		const char *text = ui_TextEntryGetText((UITextEntry*) text_entry);
		if (!new_def->pSpecificValue) {
			new_def->pSpecificValue = StructCreate(parse_WorldVariable);
		}
		wleUIVariableTextValueForWorldVarChanged(def, new_def->pSpecificValue, text, text_entry, entry);

	} else {
		StructDestroySafe( parse_WorldVariable, &new_def->pSpecificValue );
	}

	if (new_def->eDefaultType == WVARDEF_ACTIVITY_VARIABLE)
	{
		const char* activityName = ui_TextEntryGetText(entry->varActivityNameValue);
		const char* activityVar = ui_TextEntryGetText(entry->varActivityVariableNameValue);
		const char *activityDefault = ui_TextEntryGetText(entry->varSimpleValue);
		if (!new_def->activity_default_value) {
			new_def->activity_default_value = StructCreate(parse_WorldVariable);
		}
		new_def->activity_name = StructAllocString(activityName);
		new_def->activity_variable_name = StructAllocString(activityVar);
		wleUIVariableTextValueForWorldVarChanged(def, new_def->activity_default_value, activityDefault, text_entry, entry);
	}
	else
	{
		new_def->activity_name = NULL;
		new_def->activity_variable_name = NULL;
		StructDestroySafe( parse_WorldVariable, &new_def->activity_default_value );
	}

	if( new_def->eDefaultType == WVARDEF_CHOICE_TABLE ) {
		const char* choiceTableName = ui_TextEntryGetText(entry->varChoiceTableValue);
		const char* choiceName = ui_TextEntryGetText(entry->varChoiceNameValue);
		SET_HANDLE_FROM_STRING( "ChoiceTable", choiceTableName, new_def->choice_table );
		new_def->choice_name = StructAllocString(choiceName);
		new_def->choice_index = entry->varChoiceIndexValue ? ui_SpinnerEntryGetValue(entry->varChoiceIndexValue) : 0;
	} else {
		REMOVE_HANDLE( new_def->choice_table );
		new_def->choice_name = NULL;
	}

	wleOpModifyVariable(entry->index, new_def);

	StructDestroy(parse_WorldVariableDef, new_def);
}

static void wleUIVariableExpressionValueChanged(UIExpressionEntry *expressionEntry, VariableDefPropertiesEntry *entry)
{
	WorldVariableDef *def = zmapInfoGetVariableDef(NULL, entry->index);
	WorldVariableDef *newDef = StructClone(parse_WorldVariableDef, def);
	const char *expressionText = ui_ExpressionEntryGetText(expressionEntry);

	assert(newDef);
	exprDestroy(newDef->pExpression);
	newDef->pExpression = NULL;

	if (newDef->eDefaultType == WVARDEF_EXPRESSION && EMPTY_TO_NULL(expressionText)) {		
		newDef->pExpression = exprCreateFromString(expressionText, zmapGetFilename(NULL));
	}

	wleOpModifyVariable(entry->index, newDef);

	StructDestroy(parse_WorldVariableDef, newDef);
}

static void wleUIVariableMessageValueChanged(UIMessageEntry *msg_entry, VariableDefPropertiesEntry *entry)
{
	WorldVariableDef *def = zmapInfoGetVariableDef(NULL, entry->index);
	WorldVariableDef *new_def = StructClone(parse_WorldVariableDef, def);
	assert(new_def);

	assertmsg(def->eType == WVAR_MESSAGE, "Unexpected type in variable value message change callback");

	REMOVE_HANDLE(new_def->pSpecificValue->messageVal.hMessage);
	StructDestroy(parse_Message, new_def->pSpecificValue->messageVal.pEditorCopy);
	new_def->pSpecificValue->messageVal.pEditorCopy = StructClone(parse_Message, ui_MessageEntryGetMessage(msg_entry));

	wleOpModifyVariable(entry->index, new_def);

	StructDestroy(parse_WorldVariableDef, new_def);
}

static void wleUIVariablePublicChanged(UICheckButton *button, VariableDefPropertiesEntry *entry)
{
	WorldVariableDef *def = zmapInfoGetVariableDef(NULL, entry->index);
	WorldVariableDef *new_def = StructClone(parse_WorldVariableDef, def);
	assert(new_def);

	new_def->bIsPublic = button->state;

	wleOpModifyVariable(entry->index, new_def);

	StructDestroy(parse_WorldVariableDef, new_def);
}

static F32 wleUIVariableDefPropertiesRefreshSpecifyDefault(VariableDefPropertiesEntry *entry, const char* label,  WorldVariableDef *var_def, WorldVariable **ppvar, F32 y, EMPanel *panel, int index) 
{
	char buf[1024];
	if (!entry->valueLabel && var_def->eType != WVAR_MAP_POINT) 
	{
		entry->valueLabel = ui_LabelCreate(label, 20, y);
		emPanelAddChild(panel, entry->valueLabel, false);
		ui_WidgetSetPosition(UI_WIDGET(entry->valueLabel),20,y);
	}

	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch (var_def->eType)
	{
	case WVAR_INT: 
	case WVAR_FLOAT: case WVAR_STRING:
	case WVAR_LOCATION_STRING:
		if (!entry->varSimpleValue)
		{
			entry->varSimpleValue = ui_TextEntryCreate("", 120, y);
			ui_TextEntrySetFinishedCallback(entry->varSimpleValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varSimpleValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varSimpleValue, false);
		}
		ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
		ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
		ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
		ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
		ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
		break;

	case WVAR_ANIMATION:
		if (!entry->varAnimValue)
		{
			entry->varAnimValue = ui_TextEntryCreateWithGlobalDictionaryCombo("", 120, y, "AIAnimList", "ResourceName", true, true, true, true);
			ui_TextEntrySetFinishedCallback(entry->varAnimValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varAnimValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varAnimValue, false);
		}
		ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
		ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
		ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
		ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
		ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
		break;

	case WVAR_CRITTER_DEF:
		if (!entry->varCritterDefValue)
		{
			entry->varCritterDefValue = ui_TextEntryCreateWithGlobalDictionaryCombo("", 120, y, "CritterDef", "ResourceName", true, true, true, true);
			ui_TextEntrySetFinishedCallback(entry->varCritterDefValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varCritterDefValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varCritterDefValue, false);
		}
		ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
		ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
		ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
		ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
		ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
		ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
		break;

	case WVAR_CRITTER_GROUP:
		if (!entry->varCritterGroupValue)
		{
			entry->varCritterGroupValue = ui_TextEntryCreateWithGlobalDictionaryCombo("", 120, y, "CritterGroup", "ResourceName", true, true, true, true);
			ui_TextEntrySetFinishedCallback(entry->varCritterGroupValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varCritterGroupValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varCritterGroupValue, false);
		}
		ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
		ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
		ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
		ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
		ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
		break;

	case WVAR_MESSAGE:
		if (!entry->varMessageValue)
		{
			entry->varMessageValue = ui_MessageEntryCreate(NULL, 120, y, 100);
			ui_MessageEntrySetChangedCallback(entry->varMessageValue, wleUIVariableMessageValueChanged, entry);
			ui_MessageEntrySetCanEditKey(entry->varMessageValue, false);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varMessageValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varMessageValue, false);
		}
		ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
		ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
		ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
		ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
		ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
		break;

	case WVAR_ITEM_DEF:
		if (!entry->varItemDefValue)
		{
			entry->varItemDefValue = ui_TextEntryCreateWithGlobalDictionaryCombo("", 120, y, "ItemDef", "ResourceName", true, true, true, true);
			ui_TextEntrySetFinishedCallback(entry->varItemDefValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varItemDefValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varItemDefValue, false);
		}
		ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
		ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
		ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
		ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
		ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
		ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
		break;

	case WVAR_MISSION_DEF:
		if (!entry->varMissionDefValue)
		{
			entry->varMissionDefValue = ui_TextEntryCreateWithGlobalDictionaryCombo("", 120, y, "Mission", "ResourceName", true, true, true, true);
			ui_TextEntrySetFinishedCallback(entry->varMissionDefValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varMissionDefValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varMissionDefValue, false);
		}
		ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
		ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
		ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
		ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
		ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
		ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
		break;

	case WVAR_MAP_POINT:
		if (!entry->varMapNameValue) 
		{
			entry->mapNameLabel = ui_LabelCreate("Zone Map", 20, y);
			emPanelAddChild(panel, entry->mapNameLabel, false);
			entry->varMapNameValue = ui_TextEntryCreateWithGlobalDictionaryCombo("", 120, y, "ZoneMap", "ResourceName", true, true, true, true);
			ui_TextEntrySetFinishedCallback(entry->varMapNameValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varMapNameValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varMapNameValue, false);

			y = elUINextY(entry->varMapNameValue);

			entry->spawnPointLabel = ui_LabelCreate("Spawn Point", 20, y);
			emPanelAddChild(panel, entry->spawnPointLabel, false);
			entry->varSimpleValue = ui_TextEntryCreate("", 120, y);
			ui_TextEntrySetFinishedCallback(entry->varSimpleValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varSimpleValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varSimpleValue, false);
		} else {
			y = elUINextY(entry->varMapNameValue);
		}
		ui_WidgetQueueFreeAndNull(&entry->valueLabel);
		ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
		ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
		ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
		break;

	default:
		ui_WidgetQueueFreeAndNull(&entry->valueLabel);
		ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
		ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
		ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
		ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
		ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
		ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
		ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
	}

	// VARIABLE_TYPES: Add code below if add to the available variable types
	if (entry->varSimpleValue)
	{
		ui_WidgetSetPosition(UI_WIDGET(entry->varSimpleValue),120,y);
		y = elUINextY(entry->varSimpleValue);
	}
	else if (entry->varAnimValue)
	{
		ui_WidgetSetPosition(UI_WIDGET(entry->varAnimValue),120,y);
		y = elUINextY(entry->varAnimValue);
	}
	else if (entry->varCritterDefValue)
	{
		ui_WidgetSetPosition(UI_WIDGET(entry->varCritterDefValue),120,y);
		y = elUINextY(entry->varCritterDefValue);
	}
	else if (entry->varCritterGroupValue)
	{
		ui_WidgetSetPosition(UI_WIDGET(entry->varCritterGroupValue),120,y);
		y = elUINextY(entry->varCritterGroupValue);
	}
	else if (entry->varMessageValue)
	{
		ui_WidgetSetPosition(UI_WIDGET(entry->varMessageValue),120,y);
		y = elUINextY(entry->varMessageValue);
	}
	else if (entry->varItemDefValue)
	{
		ui_WidgetSetPosition(UI_WIDGET(entry->varItemDefValue),120,y);
		y = elUINextY(entry->varItemDefValue);
	}
	else if (entry->varMissionDefValue)
	{
		ui_WidgetSetPosition(UI_WIDGET(entry->varMissionDefValue),120,y);
		y = elUINextY(entry->varMissionDefValue);
	}

	if (!(*ppvar))
		(*ppvar) = StructCreate(parse_WorldVariable);
	if ((*ppvar)->eType != var_def->eType)
		(*ppvar)->eType = var_def->eType;

	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch (var_def->eType)
	{
	case WVAR_INT:
		sprintf(buf, "%d", (*ppvar)->iIntVal);
		ui_TextEntrySetText(entry->varSimpleValue, buf);
		break;
	case WVAR_FLOAT:
		sprintf(buf, "%g", (*ppvar)->fFloatVal);
		ui_TextEntrySetText(entry->varSimpleValue, buf);
		break;
	case WVAR_STRING:
	case WVAR_LOCATION_STRING:
		if ((*ppvar)->pcStringVal) 
			strcpy(buf, (*ppvar)->pcStringVal);
		else
			buf[0] = '\0';
		ui_TextEntrySetText(entry->varSimpleValue, buf);
		break;
	case WVAR_ANIMATION:
		if ((*ppvar)->pcStringVal) 
			strcpy(buf, (*ppvar)->pcStringVal);
		else
			buf[0] = '\0';
		ui_TextEntrySetText(entry->varAnimValue, buf);
		break;
	case WVAR_CRITTER_DEF:
		if (REF_STRING_FROM_HANDLE((*ppvar)->hCritterDef))
			strcpy(buf, REF_STRING_FROM_HANDLE((*ppvar)->hCritterDef));
		else
			buf[0] = '\0';
		ui_TextEntrySetText(entry->varCritterDefValue, buf);
		break;
	case WVAR_CRITTER_GROUP:
		if (REF_STRING_FROM_HANDLE((*ppvar)->hCritterGroup))
			strcpy(buf, REF_STRING_FROM_HANDLE((*ppvar)->hCritterGroup));
		else
			buf[0] = '\0';
		ui_TextEntrySetText(entry->varCritterGroupValue, buf);
		break;
	case WVAR_MESSAGE:
		if ((*ppvar)->messageVal.pEditorCopy)
			ui_MessageEntrySetMessage(entry->varMessageValue, (*ppvar)->messageVal.pEditorCopy);
		else if (GET_REF((*ppvar)->messageVal.hMessage))
			ui_MessageEntrySetMessage(entry->varMessageValue, GET_REF((*ppvar)->messageVal.hMessage));
		else
		{
			(*ppvar)->messageVal.pEditorCopy = langCreateMessage(zmapGetDefaultVariableMsgKey(NULL,index), "A variable display string", zmapInfoGetDefaultMessageScope(NULL), "");
			ui_MessageEntrySetMessage(entry->varMessageValue, (*ppvar)->messageVal.pEditorCopy);
		}
		break;
	case WVAR_ITEM_DEF:
		if ((*ppvar)->pcStringVal) 
			strcpy(buf, (*ppvar)->pcStringVal);
		else
			buf[0] = '\0';
		ui_TextEntrySetText(entry->varItemDefValue, buf);
		break;
	case WVAR_MISSION_DEF:
		if ((*ppvar)->pcStringVal) 
			strcpy(buf, (*ppvar)->pcStringVal);
		else
			buf[0] = '\0';
		ui_TextEntrySetText(entry->varMissionDefValue, buf);
		break;
	}
	return y;
}

static F32 wleUIVariableDefPropertiesRefresh(WorldVariableDef *var_def, int index, VariableDefPropertiesEntry *entry, F32 y)
{
	EMPanel *panel = editorUIState->variablePropertiesUI.variablePanel;
	char buf[1024];

	entry->index = index;

	if (!entry->removeButton)
	{
		sprintf(buf, "Variable #%d", index+1);
		entry->removeButton = ui_CheckButtonCreate(0, y, buf, true);
		ui_CheckButtonSetToggledCallback(entry->removeButton, wleUIRemoveVariable, entry);
		emPanelAddChild(panel, entry->removeButton, false);
	}
	ui_CheckButtonSetState(entry->removeButton, true);
	ui_WidgetSetPosition(UI_WIDGET(entry->removeButton),0,y);
	y = elUINextY(entry->removeButton);

	if (!entry->nameLabel) 
	{
		entry->nameLabel = ui_LabelCreate("Variable Name", 20, y);
		emPanelAddChild(panel, entry->nameLabel, false);
		entry->nameEntry = ui_TextEntryCreate("", 120, y);
		ui_TextEntrySetFinishedCallback(entry->nameEntry, wleUIVariableNameChanged, entry);
		ui_WidgetSetWidthEx(UI_WIDGET(entry->nameEntry), 1, UIUnitPercentage);
		emPanelAddChild(panel, entry->nameEntry, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(entry->nameLabel),20,y);
	ui_WidgetSetPosition(UI_WIDGET(entry->nameEntry),120,y);
	ui_TextEntrySetText(entry->nameEntry, var_def->pcName);
	y = elUINextY(entry->nameEntry);

	if (!entry->typeLabel) 
	{
		entry->typeLabel = ui_LabelCreate("Variable Type", 20, y);
		emPanelAddChild(panel, entry->typeLabel, false);
		entry->typeCombo = ui_ComboBoxCreateWithEnum(120, y, 140, WorldVariableTypeEnum, wleUIVariableTypeChanged, entry);
		emPanelAddChild(panel, entry->typeCombo, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(entry->typeLabel),20,y);
	ui_WidgetSetPosition(UI_WIDGET(entry->typeCombo),120,y);
	ui_ComboBoxSetSelectedEnum(entry->typeCombo, var_def->eType);
	y = elUINextY(entry->typeCombo);

	if (!entry->defaultLabel) 
	{
		entry->defaultLabel = ui_LabelCreate("Defaults To", 20, y);
		emPanelAddChild(panel, entry->defaultLabel, false);
		entry->defaultValueCombo = ui_ComboBoxCreateWithEnum(120, y, 160, WorldVariableDefaultValueTypeSansVariablesEnum, wleUIVariableDefaultTypeChanged, entry);
		emPanelAddChild(panel, entry->defaultValueCombo, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(entry->defaultLabel),20,y);
	ui_WidgetSetPosition(UI_WIDGET(entry->defaultValueCombo),120,y);
	ui_ComboBoxSetSelectedEnum(entry->defaultValueCombo, var_def->eDefaultType);
	y = elUINextY(entry->defaultValueCombo);

	if ( var_def->eDefaultType == WVARDEF_SPECIFY_DEFAULT ) 
	{
		y = wleUIVariableDefPropertiesRefreshSpecifyDefault(entry, "Value", var_def, &var_def->pSpecificValue, y, panel, index);
	} else {
		// We're reusing these widgets for the Activity variables below as well
		if (var_def->eDefaultType != WVARDEF_ACTIVITY_VARIABLE)
		{
			ui_WidgetQueueFreeAndNull(&entry->valueLabel);
			ui_WidgetQueueFreeAndNull(&entry->varSimpleValue);
			ui_WidgetQueueFreeAndNull(&entry->varAnimValue);
			ui_WidgetQueueFreeAndNull(&entry->varCritterDefValue);
			ui_WidgetQueueFreeAndNull(&entry->varCritterGroupValue);
			ui_WidgetQueueFreeAndNull(&entry->varMessageValue);
			ui_WidgetQueueFreeAndNull(&entry->varItemDefValue);
			ui_WidgetQueueFreeAndNull(&entry->varMissionDefValue);
			ui_WidgetQueueFreeAndNull(&entry->varMapNameValue);
			ui_WidgetQueueFreeAndNull(&entry->spawnPointLabel);
			ui_WidgetQueueFreeAndNull(&entry->mapNameLabel);
		}
	}

	if ( var_def->eDefaultType == WVARDEF_CHOICE_TABLE ) {
		ChoiceTable *choiceTable = GET_REF(var_def->choice_table);

		if (!entry->choiceTableLabel) {
			entry->choiceTableLabel = ui_LabelCreate("Choice Table", 20, y);
			emPanelAddChild(panel, entry->choiceTableLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->choiceTableLabel),20,y);

		if (!entry->varChoiceTableValue) {
			entry->varChoiceTableValue = ui_TextEntryCreateWithGlobalDictionaryCombo("", 120, y, "ChoiceTable", "ResourceName", true, true, true, true );
			ui_TextEntrySetFinishedCallback(entry->varChoiceTableValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varChoiceTableValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varChoiceTableValue, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->varChoiceTableValue), 120, y);
		y = elUINextY(entry->varChoiceTableValue);

		if (IS_HANDLE_ACTIVE(var_def->choice_table)) {
			strcpy(buf, REF_STRING_FROM_HANDLE(var_def->choice_table));
		} else {
			buf[0] = '\0';
		}
		ui_TextEntrySetText(entry->varChoiceTableValue, buf);

		if (!entry->choiceNameLabel) {
			entry->choiceNameLabel = ui_LabelCreate("Choice Value", 20, y);
			emPanelAddChild(panel, entry->choiceNameLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->choiceNameLabel),20,y);

		if (!entry->varChoiceNameValue) {
			entry->varChoiceNameValue = ui_TextEntryCreateWithStringCombo("", 120, y, &entry->choiceTableNames, true, true, true, false);
			ui_TextEntrySetFinishedCallback(entry->varChoiceNameValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varChoiceNameValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varChoiceNameValue, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->varChoiceNameValue), 120, y);
		y = elUINextY(entry->varChoiceNameValue);

		if (choiceTable && choiceTable->eSelectType == CST_TimedRandom) {
			if (!entry->choiceIndexLabel) {
				entry->choiceIndexLabel = ui_LabelCreate("Choice Index", 20, y);
				emPanelAddChild(panel, entry->choiceIndexLabel, false);
			}
			ui_WidgetSetPosition(UI_WIDGET(entry->choiceIndexLabel),20,y);

			if (!entry->varChoiceIndexValue) {
				entry->varChoiceIndexValue = ui_SpinnerEntryCreate(1, choice_TimedRandomValuesPerInterval(choiceTable), 1, var_def->choice_index, false);
				ui_SpinnerEntrySetCallback(entry->varChoiceIndexValue, wleUIVariableTextValueChanged, entry);
				ui_WidgetSetWidthEx(UI_WIDGET(entry->varChoiceIndexValue), 1, UIUnitPercentage);
				emPanelAddChild(panel, entry->varChoiceIndexValue, false);
			}
			ui_WidgetSetPosition(UI_WIDGET(entry->varChoiceIndexValue), 120, y);
			y = elUINextY(entry->varChoiceIndexValue);
		}

		if (var_def->choice_name) {
			ui_TextEntrySetText(entry->varChoiceNameValue, var_def->choice_name);
		}

		eaDestroyEx(&entry->choiceTableNames, NULL);
		entry->choiceTableNames = choice_ListNames( REF_STRING_FROM_HANDLE( var_def->choice_table ));
	} else {
		ui_WidgetQueueFreeAndNull(&entry->choiceTableLabel);
		ui_WidgetQueueFreeAndNull(&entry->varChoiceTableValue);
		ui_WidgetQueueFreeAndNull(&entry->choiceNameLabel);
		ui_WidgetQueueFreeAndNull(&entry->varChoiceNameValue);
		ui_WidgetQueueFreeAndNull(&entry->choiceIndexLabel);
		ui_WidgetQueueFreeAndNull(&entry->varChoiceIndexValue);
	}

	if ( var_def->eDefaultType == WVARDEF_EXPRESSION ) {
		if (!entry->expressionLabel) {
			entry->expressionLabel = ui_LabelCreate("Expression", 20, y);
			emPanelAddChild(panel, entry->expressionLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->expressionLabel),20,y);

		if (!entry->varExpressionValue) {
			entry->varExpressionValue = ui_ExpressionEntryCreate("", worldVariableGetExprContext());
			ui_ExpressionEntrySetChangedCallback(entry->varExpressionValue, wleUIVariableExpressionValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varExpressionValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varExpressionValue, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->varExpressionValue), 120, y);
		y = elUINextY(entry->varExpressionValue);
		ui_ExpressionEntrySetText(entry->varExpressionValue, exprGetCompleteString(var_def->pExpression));
	} else {
		ui_WidgetQueueFreeAndNull(&entry->expressionLabel);
		ui_WidgetQueueFreeAndNull(&entry->varExpressionValue);
	}

	if ( var_def->eDefaultType == WVARDEF_ACTIVITY_VARIABLE ) {
		if (!entry->activityNameLabel) {
			entry->activityNameLabel = ui_LabelCreate("Activity Name", 20, y);
			emPanelAddChild(panel, entry->activityNameLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->activityNameLabel),20,y);

		if (!entry->varActivityNameValue) {
			entry->varActivityNameValue = ui_TextEntryCreate("", 120, y);
			ui_TextEntrySetFinishedCallback(entry->varActivityNameValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varActivityNameValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varActivityNameValue, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->varActivityNameValue), 120, y);
		y = elUINextY(entry->varActivityNameValue);

		if (!entry->activityVariableNameLabel) {
			entry->activityVariableNameLabel = ui_LabelCreate("Activity Var", 20, y);
			emPanelAddChild(panel, entry->activityVariableNameLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->activityVariableNameLabel),20,y);

		if (!entry->varActivityVariableNameValue) {
			entry->varActivityVariableNameValue = ui_TextEntryCreate("", 120, y);
			ui_TextEntrySetFinishedCallback(entry->varActivityVariableNameValue, wleUIVariableTextValueChanged, entry);
			ui_WidgetSetWidthEx(UI_WIDGET(entry->varActivityVariableNameValue), 1, UIUnitPercentage);
			emPanelAddChild(panel, entry->varActivityVariableNameValue, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(entry->varActivityVariableNameValue), 120, y);
		y = elUINextY(entry->varActivityVariableNameValue);

		y = wleUIVariableDefPropertiesRefreshSpecifyDefault(entry, "Default Value", var_def, &var_def->activity_default_value, y, panel, index);

	} else {
		ui_WidgetQueueFreeAndNull(&entry->activityNameLabel);
		ui_WidgetQueueFreeAndNull(&entry->activityVariableNameLabel);
		ui_WidgetQueueFreeAndNull(&entry->activityDefaultValueLabel);
		ui_WidgetQueueFreeAndNull(&entry->varActivityNameValue);
		ui_WidgetQueueFreeAndNull(&entry->varActivityVariableNameValue);
	}

	if (!entry->isPublicButton)
	{
		entry->isPublicButton = ui_CheckButtonCreate(20, y, "Public", var_def->bIsPublic);
		ui_CheckButtonSetToggledCallback(entry->isPublicButton, wleUIVariablePublicChanged, entry);
		emPanelAddChild(panel, entry->isPublicButton, false);
	}
	ui_CheckButtonSetState(entry->isPublicButton, var_def->bIsPublic);
	ui_WidgetSetPosition(UI_WIDGET(entry->isPublicButton),20,y);
	y = elUINextY(entry->isPublicButton);

	return y;
}




void wleUIVariablePropertiesRefresh(void)
{
	F32 y = 0;
	int numVars = zmapInfoGetVariableCount(NULL);
	int i;

	for(i=0; i<numVars; ++i) 
	{
		WorldVariableDef *varDef = zmapInfoGetVariableDef(NULL, i);
		if (i >= eaSize(&editorUIState->variablePropertiesUI.entries))
		{
			VariableDefPropertiesEntry *entry = calloc(1, sizeof(VariableDefPropertiesEntry));
			eaPush(&editorUIState->variablePropertiesUI.entries, entry);
		}
		assert(editorUIState->variablePropertiesUI.entries);
		y = wleUIVariableDefPropertiesRefresh(varDef, i, editorUIState->variablePropertiesUI.entries[i], y);
	}
	for(; i<eaSize(&editorUIState->variablePropertiesUI.entries); )
	{
		wleUIVariableDefPropertiesFree(editorUIState->variablePropertiesUI.entries[i]);
		eaRemove(&editorUIState->variablePropertiesUI.entries, i);
	}

	if (!editorUIState->variablePropertiesUI.addButton)
	{
		editorUIState->variablePropertiesUI.addButton = ui_ButtonCreate("Add Variable", 0, y, wleUIAddVariable, NULL);
		ui_WidgetSetWidth(UI_WIDGET(editorUIState->variablePropertiesUI.addButton), 100);
		emPanelAddChild(editorUIState->variablePropertiesUI.variablePanel, editorUIState->variablePropertiesUI.addButton, true);
	}
	ui_WidgetSetPosition(UI_WIDGET(editorUIState->variablePropertiesUI.addButton), 0, y);

	emPanelUpdateHeight(editorUIState->variablePropertiesUI.variablePanel);
}

/****************************
* IN-DIALOG FOLDER FUNCTIONS
****************************/

static char *ui_folder_mock_parent = NULL;
static char *ui_folder_mock_node = NULL;

static void wleUIFolderNodeDisplay(UITreeNode *node, void *unused, UI_MY_ARGS, F32 z)
{
	char display[MAX_PATH];
	char *dirname = strrchr((char*)node->contents, '/');
	if (dirname)
		strcpy(display, dirname+1);
	else
		strcpy(display, (char*)node->contents);

	ui_TreeDisplayText(node, display, UI_MY_VALUES, z);
}

static void wleUIFolderNodeFill(UITreeNode *parent_node, void *unused);

void wleUIFolderNodeFree(UITreeNode *node)
{
	SAFE_FREE(node->contents);
}

FileScanAction wleUIFolderNodeFillProc(char* dir, struct _finddata32_t* data, UITreeNode *parent_node)
{
	if (data->attrib & _A_SUBDIR)
	{
		size_t path_size = strlen(dir) + strlen(data->name) + 2;
		char *path = calloc(path_size, 1);
		UITreeNode *newNode;
		sprintf_s(SAFESTR2(path), "%s/%s", dir, data->name);
		newNode = ui_TreeNodeCreate(
			parent_node->tree, cryptAdler32String(path), NULL, path,
			wleUIFolderNodeFill, NULL,
			wleUIFolderNodeDisplay, NULL, 13);
		ui_TreeNodeSetFreeCallback(newNode, wleUIFolderNodeFree);
		ui_TreeNodeAddChild(parent_node, newNode);
	}
	return FSA_NO_EXPLORE_DIRECTORY;
}

static void wleUIFolderNodeFill(UITreeNode *parent_node, void *unused)
{
	fileScanAllDataDirs((char*)parent_node->contents, wleUIFolderNodeFillProc, parent_node);
	if (ui_folder_mock_parent && ui_folder_mock_node)
	{
		int i;
		for (i = 0; i < eaSize(&parent_node->children); i++)
			if (stricmp(ui_folder_mock_node, parent_node->children[i]->contents) == 0)
				return;
		if (stricmp(ui_folder_mock_parent, parent_node->contents) == 0)
		{
			UITreeNode *newNode;
			char *path = strdup(ui_folder_mock_node);
			newNode = ui_TreeNodeCreate(
				parent_node->tree, cryptAdler32String(path), NULL, path,
				wleUIFolderNodeFill, NULL,
				wleUIFolderNodeDisplay, NULL, 13);
			ui_TreeNodeSetFreeCallback(newNode, wleUIFolderNodeFree);
			ui_TreeNodeAddChild(parent_node, newNode);
		}
	}
}

static void wleUIFolderRootFill(UITreeNode *root, char *root_str)
{
	UITreeNode *newNode;
	char *str = root_str;
	PERFINFO_AUTO_START_FUNC();
	newNode = ui_TreeNodeCreate(root->tree, 1, NULL, (void*)str,
		wleUIFolderNodeFill, NULL, wleUIFolderNodeDisplay, NULL, 13);
	ui_TreeNodeAddChild(root, newNode);
	PERFINFO_AUTO_STOP();
}

static void wleUIExpandFolderTree(UITreeNode *node, const char *path)
{
	int i;
	for (i = 0; i < eaSize(&node->children); i++)
	{
		char *node_name = (char*)node->children[i]->contents;
		if (strnicmp(node_name, path, strlen(node_name)) == 0)
		{
			ui_TreeNodeExpand(node->children[i]);
			if (stricmp(node_name, path) == 0)
				node->tree->selected = node->children[i];
			else
				wleUIExpandFolderTree(node->children[i], path);
		}
	}
}

typedef struct wleUIFolderSelectUI
{
	UITree *dest_tree;
	UIWindow *folder_window;
	UITextEntry *folder_name_entry;
} wleUIFolderSelectUI;

void wleUIFolderNameOk(UIButton *button, wleUIFolderSelectUI *ui)
{
	UITreeNode *selected_node = ui_TreeGetSelected(ui->dest_tree);
	const char *folder_name = ui_TextEntryGetText(ui->folder_name_entry);

	if (!folder_name || !strlen(folder_name))
	{
		emStatusPrintf("Invalid directory name!");
		return;
	}

	if (selected_node)
	{
		char temp_path[MAX_PATH], out_path[MAX_PATH];
		const char *directory = selected_node->contents;
		sprintf(temp_path, "%s/%s/", directory, folder_name);
		fileLocateWrite(temp_path, out_path);
		mkdirtree(out_path);
		
		// Force the new folder to show in the UI
		SAFE_FREE(ui_folder_mock_parent);
		ui_folder_mock_parent = strdup(directory);
		SAFE_FREE(ui_folder_mock_node);
		ui_folder_mock_node = strdup(temp_path);
		ui_folder_mock_node[strlen(ui_folder_mock_node)-1] = 0;

		// Refresh the UI
		ui_TreeNodeCollapse(&ui->dest_tree->root);
		ui_TreeNodeExpand(&ui->dest_tree->root);
		temp_path[strlen(temp_path)-1] = 0;
		wleUIExpandFolderTree(&ui->dest_tree->root, temp_path);
	}

	// close the last window
	ui_WidgetQueueFree(UI_WIDGET(ui->folder_window));
}

void wleUIFolderCreateNew(UIButton *parent_button, wleUIFolderSelectUI *ui)
{
	UILabel *label;
	UIButton *button;
	UITreeNode *selected_node = ui_TreeGetSelected(ui->dest_tree);

	if (!selected_node)
		return;

	ui->folder_window = ui_WindowCreate("New Folder", 0, 0, 300, 80);
	ui_WindowSetCloseCallback(ui->folder_window, elUIWindowClose, ui->folder_window);

	label = ui_LabelCreate("New folder name:", 5, 5);
	ui_WindowAddChild(ui->folder_window, label);
	ui->folder_name_entry = ui_TextEntryCreate("New Folder", 0, label->widget.y);
	ui->folder_name_entry->widget.width = 1;
	ui->folder_name_entry->widget.widthUnit = UIUnitPercentage;
	ui->folder_name_entry->widget.leftPad = elUINextX(label) + 5;
	ui_WindowAddChild(ui->folder_window, ui->folder_name_entry);

	button = elUIAddCancelOkButtons(ui->folder_window, NULL, NULL, wleUIFolderNameOk, ui);
	elUICenterWindow(ui->folder_window);
	ui_WindowSetModal(ui->folder_window, true);
	ui_WindowShow(ui->folder_window);
	ui_SetFocus(ui->folder_name_entry);
}

/********************
* NEW ZONEMAP DIALOG
********************/
typedef struct WleUINewZoneMapWin
{
	UIWindow *win;
	UITextEntry *publicNameEntry;
	UITextEntry *widthEntry, *lengthEntry;
	UIComboBox *indoorCombo;
	UIComboBox *nameSpaceCombo;
	UIButton *okButton;
	UICheckButton *subfolderCheck;

	wleUIFolderSelectUI folder_ui;
} WleUINewZoneMapWin;

static bool checkMapDir(const char *dir, const char *action)
{
	if (stricmp(dir, "maps/system")==0 || strStartsWith(dir, "maps/system/"))
	{
		Alertf("You are not allowed to %s maps in the system folder!", action);
		return false;
	}
	if (strstri(dir, "/_"))
	{
		Alertf("You are not allowed to %s maps in hidden folders!", action);
		return false;
	}
	return true;
}

static void wleUINewZoneMapDialogOk(UIButton *button, WleUINewZoneMapWin *ui)
{
	int width, length;
	bool createSubfolder;
	WleUINewMapType map_type;
	const char *publicName = ui_TextEntryGetText(ui->publicNameEntry);
	const char *nameSpace = ui_ComboBoxGetSelectedObject(ui->nameSpaceCombo);
	char nameSpacedPublicName[MAX_PATH];
	UITreeNode *selected_node = ui_TreeGetSelected(ui->folder_ui.dest_tree);
	const char *dir;
	double x, y;

	if (!selected_node)
		return;

	dir = (char*)selected_node->contents;

	{
		const char *s;
		for ( s=publicName; s[0]; s++  ) {
			if(!(isalnum(s[0]) || s[0] == '_')) {
				emStatusPrintf("Map name can only be numbers, leters or underlines.");
				return;
			}
		}
	}

	if(nameSpace == NO_NAMESPACE_STR) {
		if (!stricmp(dir, "maps"))
		{
			emStatusPrintf("Cannot save map to root! Pick a subdirectory to save to.");
			return;
		}
		sprintf(nameSpacedPublicName, "%s", publicName);
	} else {
		char buf[MAX_PATH];
		sprintf(buf, "ns/%s/maps", nameSpace);
		if (!stricmp(dir, buf))
		{
			emStatusPrintf("Cannot save map to root! Pick a subdirectory to save to.");
			return;
		}
		sprintf(nameSpacedPublicName, "%s:%s", nameSpace, publicName);
	}

	if (!checkMapDir(dir, "create"))
	{
		return;
	}

	// round the dimensions up to the nearest increment of 256 feet
	x = (double) atoi(ui_TextEntryGetText(ui->widthEntry)) / 256.0;
	y = (double) atoi(ui_TextEntryGetText(ui->lengthEntry)) / 256.0;

	if (worldGetZoneMapByPublicName(nameSpacedPublicName))
	{
		emStatusPrintf("\"%s\" is already being used as a public name by another map.", nameSpacedPublicName);
		return;
	}

	width = ceil(x);
	length = ceil(y);
	createSubfolder = ui_CheckButtonGetState(ui->subfolderCheck);
	map_type = ui_ComboBoxGetSelectedEnum(ui->indoorCombo);

	if (map_type != WLEUI_NEW_OUTDOOR_MAP || (width > 0 && length > 0))
	{
		char filename[MAX_PATH];
		sprintf(filename, "%s.Zone", publicName);
		wleOpNewZoneMap(dir, filename, width, length, map_type, ZMTYPE_UNSPECIFIED, NULL, nameSpacedPublicName, createSubfolder);
		elUIWindowClose(NULL, ui->win);
		SAFE_FREE(ui);
	}
}

static bool wleUINewZoneMapDialogCancel(UIButton *button, WleUINewZoneMapWin *ui)
{
	elUIWindowClose(NULL, ui->win);
	ui_WidgetQueueFree(UI_WIDGET(ui->nameSpaceCombo));
	SAFE_FREE(ui);
	return true;
}

void wleUINewZoneMapDialogValidate(UserData unused, WleUINewZoneMapWin *ui)
{
	const char *publicName = ui_TextEntryGetText(ui->publicNameEntry);
	bool okActive = true;
	WleUINewMapType map_type;

	if (!publicName || !publicName[0])
		okActive = false;

	map_type = ui_ComboBoxGetSelectedEnum(ui->indoorCombo);
	if (okActive && map_type == WLEUI_NEW_OUTDOOR_MAP &&
		(ui_TextEntryGetText(ui->lengthEntry)[0] == '\0' || ui_TextEntryGetText(ui->widthEntry)[0] == '\0'))
		okActive = false;

	ui_SetActive(UI_WIDGET(ui->okButton), okActive);
}

static void  wleUINewZoneMapNameSpaceComboCB(UIComboBox *combo, WleUINewZoneMapWin *ui)
{
	static char fillData[MAX_PATH];
	const char *selectedNameSpace = ui_ComboBoxGetSelectedObject(combo);
	if(selectedNameSpace == NO_NAMESPACE_STR) {
		ui->folder_ui.dest_tree->root.fillData = "maps";
	} else {
		sprintf(fillData, "ns/%s/maps", selectedNameSpace);
		ui->folder_ui.dest_tree->root.fillData = fillData;
	}
	ui_TreeNodeExpand(&ui->folder_ui.dest_tree->root);
}

void wleUINewZoneMapTypeComboCB(UserData unused, int type, WleUINewZoneMapWin *ui)
{
	if(type == WLEUI_NEW_OUTDOOR_MAP)
	{
		ui_SetActive(UI_WIDGET(ui->lengthEntry), true);
		ui_SetActive(UI_WIDGET(ui->widthEntry), true);
	}
	else
	{
		ui_SetActive(UI_WIDGET(ui->lengthEntry), false);
		ui_SetActive(UI_WIDGET(ui->widthEntry), false);
	}

	wleUINewZoneMapDialogValidate(NULL, ui);
}

void wleUINewZoneMapDialogCreate(void)
{
	static const char **nameSpaceList = NULL;
	const char **fileNameSpaces = fileGetNameSpaceNameList();
	WleUINewZoneMapWin *ui;
	UIWindow *win;
	UILabel *label;
	UITextEntry *entry;
	UIButton *button;
	UIComboBox *combo;
	UICheckButton *check;
	char expand_filename[MAX_PATH], *expand_delim;
	
	ui = calloc(1, sizeof(*ui));

	win = ui_WindowCreate("New ZoneMap", 100, 100, 400, 90);
	ui->win = win;

	label = ui_LabelCreate("Map Name", 5, 5);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetChangedCallback(entry, wleUINewZoneMapDialogValidate, ui);
	ui_TextEntrySetEnterCallback(entry, wleUINewZoneMapDialogOk, ui);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	entry->widget.leftPad = 85;
	entry->widget.rightPad = 5;
	ui->publicNameEntry = entry;
	ui_WindowAddChild(win, entry);

	eaClear(&nameSpaceList);
	eaCopy(&nameSpaceList, &fileNameSpaces);
	eaInsert(&nameSpaceList, NO_NAMESPACE_STR, 0);
	label = ui_LabelCreate("Name Space", 5, elUINextY(entry) + 5);
	ui_WindowAddChild(win, label);
	combo = ui_ComboBoxCreate(0, elUINextY(entry) + 5, 1, NULL, &nameSpaceList, NULL);
	combo->widget.leftPad = 85;
	combo->widget.rightPad = 5;
	combo->widget.widthUnit = UIUnitPercentage;
	ui->nameSpaceCombo = combo;
	ui_ComboBoxSetSelected(combo, 0);
	ui_ComboBoxSetSelectedCallback(combo, wleUINewZoneMapNameSpaceComboCB, ui);
	ui_WindowAddChild(win, combo);

	combo = ui_ComboBoxCreateWithEnum(0, elUINextY(combo) + 5, 1, WleUINewMapTypeEnum, NULL, NULL);
	combo->widget.leftPad = combo->widget.rightPad = 5;
	combo->widget.widthUnit = UIUnitPercentage;
	ui->indoorCombo = combo;
	ui_ComboBoxSetSelectedEnum(combo, WLEUI_NEW_INDOOR_MAP);
	ui_ComboBoxSetSelectedEnumCallback(combo, wleUINewZoneMapTypeComboCB, ui);
	ui_WindowAddChild(win, combo);

	label = ui_LabelCreate("Width", 5, elUINextY(combo) + 5);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetChangedCallback(entry, wleUINewZoneMapDialogValidate, ui);
	ui_TextEntrySetEnterCallback(entry, wleUINewZoneMapDialogOk, ui);
	ui_TextEntrySetIntegerOnly(entry);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	ui->widthEntry = entry;
	ui_WindowAddChild(win, entry);
	label = ui_LabelCreate("ft.", 5, label->widget.y);
	label->widget.offsetFrom = UITopRight;
	ui_WindowAddChild(win, label);
	ui_SetActive(UI_WIDGET(ui->widthEntry), false);
	entry->widget.rightPad = elUINextX(label) + 5;

	label = ui_LabelCreate("Length", 5, elUINextY(label) + 5);
	entry->widget.leftPad = elUINextX(label) + 5;
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetChangedCallback(entry, wleUINewZoneMapDialogValidate, ui);
	ui_TextEntrySetEnterCallback(entry, wleUINewZoneMapDialogOk, ui);
	ui_TextEntrySetIntegerOnly(entry);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	entry->widget.leftPad = elUINextX(label) + 5;
	ui->lengthEntry = entry;
	ui_WindowAddChild(win, entry);
	label = ui_LabelCreate("ft.", 5, label->widget.y);
	label->widget.offsetFrom = UITopRight;
	ui_WindowAddChild(win, label);
	ui_SetActive(UI_WIDGET(ui->lengthEntry), false);
	entry->widget.rightPad = elUINextX(label) + 5;

	check = ui_CheckButtonCreate(5, elUINextY(entry) + 5, "Create subfolder", true);
	ui->subfolderCheck = check;
	ui_WindowAddChild(win, check);

	ui->folder_ui.dest_tree = ui_TreeCreate(5, elUINextY(ui->subfolderCheck), 1, 400);
	ui->folder_ui.dest_tree->widget.widthUnit = UIUnitPercentage;
	ui->folder_ui.dest_tree->root.fillF = wleUIFolderRootFill;
	ui->folder_ui.dest_tree->root.fillData = "maps";
	ui_TreeNodeExpand(&ui->folder_ui.dest_tree->root);
	strcpy(expand_filename, zmapGetFilename(NULL));
	if (expand_delim = strrchr(expand_filename, '/'))
		*expand_delim = 0;
	wleUIExpandFolderTree(&ui->folder_ui.dest_tree->root, expand_filename);
	ui_WindowAddChild(win, ui->folder_ui.dest_tree);

	button = ui_ButtonCreate("New Folder...", 5, 5, wleUIFolderCreateNew, &ui->folder_ui);
	button->widget.width = 100;
	button->widget.offsetFrom = UIBottomLeft;
	ui_WindowAddChild(win, button);

	button = elUIAddCancelOkButtons(win, wleUINewZoneMapDialogCancel, ui, wleUINewZoneMapDialogOk, ui);
	ui->okButton = button;
	ui_SetActive(UI_WIDGET(button), false);
	ui_WindowSetCloseCallback(win, wleUINewZoneMapDialogCancel, ui);
	win->widget.height = elUINextY(ui->folder_ui.dest_tree) + elUINextY(button) + 5;
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
}

/********************
* OPEN ZONEMAP DIALOG
********************/
static void wleUIOpenZoneMapDialogOk(const char *dir, const char *fileName, UserData unused)
{
	wleOpOpenZoneMap(dir, fileName);
}

void wleUIOpenZoneMapDialogCreate(void)
{
	UIWindow *browser;
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];
	const char **topDirList=NULL;
	const char **extList=NULL;
	const char *mapFilename = zmapGetFilename(NULL);

	if (!mapFilename)
		return;

	fileLocateWrite(mapFilename, startDir);
	fileLocateWrite("maps", topDir);
	backSlashes(startDir);
	backSlashes(topDir);
	getDirectoryName(startDir);
	eaPush(&topDirList, topDir);
	eaPush(&extList, "zone");
	browser = ui_FileBrowserCreateEx("Open ZoneMap", "Open", UIBrowseExisting, UIBrowseFiles, true,
								   topDirList, startDir, NULL, extList, NULL, NULL, 
								   wleUIOpenZoneMapDialogOk, NULL, NULL, NULL, NULL, true);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}
	eaDestroy(&topDirList);
	eaDestroy(&extList);
}

/********************
* SAVE ZONEMAP AS DIALOG
********************/

typedef struct WleUISaveAsWaitingWin
{
	UIWindow *win;

	char *dir;
	char *fileName;
	char *publicName;
	bool createSubfolder;
	bool layersAsReference;
	bool keepExistingReferenceLayers;

	bool manuallyCheckedKeepExistingReferenceLayers;
} WleUISaveAsWaitingWin;
WleUISaveAsWaitingWin *g_WleUISaveAsWaitingWin=NULL;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void wleZmapReadyForSaveAs(bool success)
{
	assert(g_WleUISaveAsWaitingWin);

	if(success) {
		char publicName[CRYPTIC_MAX_PATH];
		if(strStartsWith(g_WleUISaveAsWaitingWin->dir, NAMESPACE_PATH)) {
			char nameSpace[CRYPTIC_MAX_PATH];
			resExtractNameSpace(g_WleUISaveAsWaitingWin->dir, nameSpace, publicName);
			sprintf(publicName, "%s:%s", nameSpace, g_WleUISaveAsWaitingWin->publicName);
		} else {
			strcpy(publicName, g_WleUISaveAsWaitingWin->publicName);
		}

		EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SaveAs_SaveLayersAsReference", g_WleUISaveAsWaitingWin->layersAsReference);
		EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SaveAs_KeepExistingLayersAsReferences", g_WleUISaveAsWaitingWin->manuallyCheckedKeepExistingReferenceLayers);

		wleOpSaveZoneMapAs(	g_WleUISaveAsWaitingWin->dir, g_WleUISaveAsWaitingWin->fileName, 
			publicName, g_WleUISaveAsWaitingWin->createSubfolder, g_WleUISaveAsWaitingWin->layersAsReference, g_WleUISaveAsWaitingWin->keepExistingReferenceLayers);
	} else {
		Errorf("Server failed to load messages, aborting Save As");
	}

	elUIWindowClose(NULL, g_WleUISaveAsWaitingWin->win);
	SAFE_FREE(g_WleUISaveAsWaitingWin->dir);
	SAFE_FREE(g_WleUISaveAsWaitingWin->fileName);
	SAFE_FREE(g_WleUISaveAsWaitingWin->publicName);
	SAFE_FREE(g_WleUISaveAsWaitingWin);
}

typedef struct WleUISaveAsWin
{
	UIWindow *win;
	UITextEntry *publicNameEntry;
	UICheckButton *subfolderCheck;
	UICheckButton *layersAsReferenceCheck;
	UICheckButton *keepExistingReferenceLayersCheck;
	UIButton *okButton;

	char *publicName;
	bool createSubfolder;
	bool layersAsReference;
	bool keepExistingReferenceLayers;

	bool manuallyCheckedKeepExistingReferenceLayers;
} WleUISaveAsWin;

static void wleUISaveZoneMapAsDialogFileCancel(WleUISaveAsWin *saveAsUI)
{
	SAFE_FREE(saveAsUI->publicName);
	SAFE_FREE(saveAsUI);
}

static void wleUISaveZoneMapAsDialogFileOk(const char *dir, const char *fileName, WleUISaveAsWin *saveAsUI)
{
	if (!checkMapDir(dir, "save"))
		return;

	assert(!g_WleUISaveAsWaitingWin);
	{
		UIWindow *window;
		UILabel *label;
		int w, h;

		g_WleUISaveAsWaitingWin = calloc(1, sizeof(WleUISaveAsWaitingWin));

		// Create the window
		window = ui_WindowCreate("Saving Map As", 0, 0, 300, 50);
		ui_WindowSetModal(window, true);
		ui_WindowSetClosable(window, false);

		// Lay out the message
		label = ui_LabelCreate("Creating...",50,0);
		ui_WidgetSetPositionEx(UI_WIDGET(label),-label->widget.width/2, 0, 0.5, 0, UITopLeft);
		ui_WindowAddChild(window, label);

		// Show the window
		gfxGetActiveDeviceSize(&w, &h);
		ui_WidgetSetPosition((UIWidget*) window, (w / g_ui_State.scale - window->widget.width * window->widget.scale) / 2, (h / g_ui_State.scale - window->widget.height * window->widget.scale) / 2);
		ui_WindowPresent(window);

		g_WleUISaveAsWaitingWin->win = window;
		g_WleUISaveAsWaitingWin->createSubfolder = saveAsUI->createSubfolder;
		g_WleUISaveAsWaitingWin->layersAsReference = saveAsUI->layersAsReference;
		g_WleUISaveAsWaitingWin->keepExistingReferenceLayers = saveAsUI->keepExistingReferenceLayers;
		g_WleUISaveAsWaitingWin->manuallyCheckedKeepExistingReferenceLayers = saveAsUI->manuallyCheckedKeepExistingReferenceLayers;
		g_WleUISaveAsWaitingWin->dir = strdup(dir);
		g_WleUISaveAsWaitingWin->fileName = strdup(fileName);
		g_WleUISaveAsWaitingWin->publicName = saveAsUI->publicName;
		saveAsUI->publicName = NULL;
	}

	ServerCmd_editorServerPrepareZmapSaveAs();
	wleUISaveZoneMapAsDialogFileCancel(saveAsUI);
}

bool wleUISaveZoneMapAsDialogCancel(UIWidget *widget, WleUISaveAsWin *saveAsUI)
{
	elUIWindowClose(NULL, saveAsUI->win);
	SAFE_FREE(saveAsUI->publicName);
	SAFE_FREE(saveAsUI);
	return true;
}

void wleUISaveZoneMapAsDialogOk(UIWidget *widget, WleUISaveAsWin *saveAsUI)
{
	UIWindow *browser;
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];
	const char **extList=NULL;
	const char **topDirList=NULL;
	const char *mapFilename = zmapGetFilename(NULL);
	const char *publicName = ui_TextEntryGetText(saveAsUI->publicNameEntry);

	if (worldGetZoneMapByPublicName(publicName))
	{
		emStatusPrintf("\"%s\" is already being used as a public name by another map.", publicName);
		return;
	}

	// populate saveAsUI with data from widgets
	saveAsUI->publicName = publicName ? strdup(publicName) : NULL;
	saveAsUI->createSubfolder = ui_CheckButtonGetState(saveAsUI->subfolderCheck);
	saveAsUI->layersAsReference = ui_CheckButtonGetState(saveAsUI->layersAsReferenceCheck);
	saveAsUI->keepExistingReferenceLayers = ui_CheckButtonGetState(saveAsUI->keepExistingReferenceLayersCheck);

	// open file browser
	if (mapFilename)
		fileLocateWrite(mapFilename, startDir);
	else
		fileLocateWrite("maps", startDir);
	fileLocateWrite("maps", topDir);
	backSlashes(startDir);
	backSlashes(topDir);
	getDirectoryName(startDir);
	eaPush(&extList, "zone");
	eaPush(&topDirList, topDir);
	browser = ui_FileBrowserCreateEx("Save ZoneMap As", "Save", UIBrowseNew, UIBrowseFiles, true,
								   topDirList, startDir, mapFilename, extList, wleUISaveZoneMapAsDialogFileCancel,
								   saveAsUI, wleUISaveZoneMapAsDialogFileOk, saveAsUI, NULL, NULL, NULL, true);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}

	eaDestroy(&extList);
	eaDestroy(&topDirList);
	elUIWindowClose(NULL, saveAsUI->win);
}

void wleUISaveZoneMapAsDialogValidate(UIWidget *widget, WleUISaveAsWin *saveAsUI)
{
	const char *publicName = ui_TextEntryGetText(saveAsUI->publicNameEntry);
	bool okActive = true;

	if (!publicName || !publicName[0])
		okActive = false;

	ui_SetActive(UI_WIDGET(saveAsUI->okButton), okActive);
}

static void SaveLayersAsReferenceToggled(UICheckButton *layersAsReferenceCheckButton, WleUISaveAsWin *saveAsUI)
{
	if(ui_CheckButtonGetState(layersAsReferenceCheckButton))
	{
		ui_CheckButtonSetState(saveAsUI->keepExistingReferenceLayersCheck, true);

		ui_SetActive(UI_WIDGET(saveAsUI->keepExistingReferenceLayersCheck), false);
	}
	else
	{
		ui_SetActive(UI_WIDGET(saveAsUI->keepExistingReferenceLayersCheck), true);

		ui_CheckButtonSetState(saveAsUI->keepExistingReferenceLayersCheck, saveAsUI->manuallyCheckedKeepExistingReferenceLayers);
	}
}

static void KeepExistingLayerReferencesToggled(UICheckButton *keepExistingLayerReferencesCheckButton, WleUISaveAsWin *saveAsUI)
{
	saveAsUI->manuallyCheckedKeepExistingReferenceLayers = ui_CheckButtonGetState(keepExistingLayerReferencesCheckButton);
}

void wleUISaveZoneMapAsDialogCreate(void)
{
	WleUISaveAsWin *saveAsUI = calloc(1, sizeof(*saveAsUI));
	UIWindow *win = ui_WindowCreate("Save zonemap as", 0, 0, 300, 300);
	UILabel *label;
	UIButton *okButton;
	UITextEntry *entry;
	UICheckButton *check;

	saveAsUI->win = win;

	saveAsUI->layersAsReference = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SaveAs_SaveLayersAsReference", false);
	saveAsUI->manuallyCheckedKeepExistingReferenceLayers = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SaveAs_KeepExistingLayersAsReferences", false);

	label = ui_LabelCreate("Public name", 5, 5);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetChangedCallback(entry, wleUISaveZoneMapAsDialogValidate, saveAsUI);
	ui_TextEntrySetEnterCallback(entry, wleUISaveZoneMapAsDialogOk, saveAsUI);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	entry->widget.leftPad = elUINextX(label) + 5;
	entry->widget.rightPad = 5;
	saveAsUI->publicNameEntry = entry;
	ui_WindowAddChild(win, entry);

	check = ui_CheckButtonCreate(5, elUINextY(entry) + 5, "Create subfolder", true);
	saveAsUI->subfolderCheck = check;
	ui_WindowAddChild(win, check);

	check = ui_CheckButtonCreate(5, elUINextY(check) + 5, "Save layers as reference", true);
	ui_CheckButtonSetState(check, saveAsUI->layersAsReference);
	ui_CheckButtonSetToggledCallback(check, SaveLayersAsReferenceToggled, saveAsUI);
	saveAsUI->layersAsReferenceCheck = check;
	ui_WindowAddChild(win, check);

	check = ui_CheckButtonCreate(5, elUINextY(check) + 5, "Keep existing reference layers", true);
	if(ui_CheckButtonGetState(saveAsUI->layersAsReferenceCheck))
	{
		ui_CheckButtonSetState(check, true);

		ui_SetActive(UI_WIDGET(check), false);
	}
	else
		ui_CheckButtonSetState(check, saveAsUI->manuallyCheckedKeepExistingReferenceLayers);
	ui_CheckButtonSetToggledCallback(check, KeepExistingLayerReferencesToggled, saveAsUI);
	saveAsUI->keepExistingReferenceLayersCheck = check;
	ui_WindowAddChild(win, check);

	ui_WindowSetModal(win, true);
	elUICenterWindow(win);
	okButton = elUIAddCancelOkButtons(win, wleUISaveZoneMapAsDialogCancel, saveAsUI, wleUISaveZoneMapAsDialogOk, saveAsUI);
	ui_SetActive(UI_WIDGET(okButton), false);
	saveAsUI->okButton = okButton;
	ui_WindowSetCloseCallback(win, wleUISaveZoneMapAsDialogCancel, saveAsUI);
	win->widget.height = elUINextY(okButton) + elUINextY(check) + 5;
	ui_WindowShow(win);
}

/********************
* NEW LAYER DIALOG
********************/
typedef struct WleUINewLayerWin
{
	UIWindow *win;
	UITextEntry *regionName;
	char regionNameStr[MAX_PATH];
} WleUINewLayerWin;

static void wleUINewLayerDialogFileCancel(WleUINewLayerWin *newLayerUI)
{
	SAFE_FREE(newLayerUI);
}

static void wleUINewLayerDialogFileOk(const char *dir, const char *fileName, WleUINewLayerWin *newLayerUI)
{
	char fullName[CRYPTIC_MAX_PATH];

	sprintf(fullName, "%s/%s", dir, fileName);
	wleOpNewLayer(fullName, newLayerUI->regionNameStr);
	wleUITrackerTreeRefresh(NULL);
	wleUINewLayerDialogFileCancel(newLayerUI);
}

static bool wleUINewLayerDialogTypeCancel(UIWidget *widget, WleUINewLayerWin *newLayerUI)
{
	elUIWindowClose(NULL, newLayerUI->win);
	SAFE_FREE(newLayerUI);
	return true;
}

static void wleUINewLayerDialogTypeOk(UIWidget *widget, WleUINewLayerWin *newLayerUI)
{
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];
	char *ext;
	const char *filename;
	UIWindow *browser;

	strcpy(newLayerUI->regionNameStr, ui_TextEntryGetText(newLayerUI->regionName));

	ext = "layer";

	// create a file browser to determine layer file's location
	filename = zmapGetFilename(NULL);
	if(filename && worldIsZoneMapInNamespace(NULL)) {
		char nameSpace[CRYPTIC_MAX_PATH];
		char buf[CRYPTIC_MAX_PATH];
		resExtractNameSpace(filename, nameSpace, buf);
		sprintf(buf, "ns/%s/maps", nameSpace);
		fileLocateWrite(buf, topDir);
	} else {
		fileLocateWrite("maps", topDir);
	}
	if (filename)
		fileLocateWrite(filename, startDir);
	else
		strcpy(startDir, topDir);
	getDirectoryName(startDir);
	backSlashes(startDir);
	backSlashes(topDir);
	browser = ui_FileBrowserCreate("New Layer Filename", "Save", UIBrowseNewNoOverwrite, UIBrowseFiles, true,
								   topDir, startDir, NULL, ext, wleUINewLayerDialogFileCancel, newLayerUI,
								   wleUINewLayerDialogFileOk, newLayerUI);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}

	elUIWindowClose(NULL, newLayerUI->win);
}

/******
* This function initiates the new layer UI flow.
******/
void wleUINewLayerDialog()
{
	if(zmapInfoHasGenesisData(NULL))
	{
		Alertf("You can not add layers to un-frozen genesis maps");
	}
	else
	{
		UIWindow *win = ui_WindowCreate("New Layer", 100, 100, 300, 80);
		UILabel *label;
		UITextEntry *regionEntry = wleUIRegionTextEntryCreate();
		WleUINewLayerWin *newLayerUI = calloc(1, sizeof(WleUINewLayerWin));
		UIButton *button;

		label = ui_LabelCreate("New layer region:", 5, 5);
		ui_WindowAddChild(win, label);
		regionEntry->widget.x = elUINextX(label) + 5;
		regionEntry->widget.y = label->widget.y;
		newLayerUI->regionName = regionEntry;
		ui_WindowAddChild(win, regionEntry);

		button = elUIAddCancelOkButtons(win, wleUINewLayerDialogTypeCancel, newLayerUI, wleUINewLayerDialogTypeOk, newLayerUI);
		win->widget.width = elUINextX(regionEntry) + 5;
		win->widget.height = elUINextY(label) + elUINextY(button) + 5;
		newLayerUI->win = win;
		elUICenterWindow(win);
		ui_WindowSetCloseCallback(win, wleUINewLayerDialogTypeCancel, newLayerUI);
		ui_WindowSetModal(win, true);
		editorUIState->currModalWin = win;
		ui_WindowShow(win);
	}
}

/********************
* IMPORT LAYER DIALOG
********************/
void wleUIImportLayerDialogOkClicked(const char *dir, const char *file, UserData unused)
{
	char layerFilename[MAX_PATH];
	char *ext;

	sprintf(layerFilename, "%s/%s", dir, file);

	// validate file has proper layer extension
	ext = strrchr(layerFilename, '.') + 1;
	if (strcmpi(ext, "layer") != 0 && strcmpi(ext, "tlayer") != 0 && strcmpi(ext, "clayer") != 0)
	{
		emStatusPrintf("\"%s\" does not correspond to a layer file!", layerFilename);
		return;
	}

	wleOpImportLayer(layerFilename);
}

/******
* This function opens the file browser for importing a layer after making sure the user is
* notified that the map will be saved.
******/
void wleUIImportLayerDialogCreate(void)
{
	UIWindow *browser;
	char startDir[MAX_PATH];
	char **topDirs = NULL;
	char **exts = NULL;
	const char *filename;

	filename = zmapGetFilename(NULL);
	strcpy(startDir, filename);
	if(filename && worldIsZoneMapInNamespace(NULL)) {
		char nameSpace[CRYPTIC_MAX_PATH];
		char buf[CRYPTIC_MAX_PATH];
		resExtractNameSpace(filename, nameSpace, buf);
		sprintf(buf, "ns/%s/maps", nameSpace);
		eaPush(&topDirs, buf);
	} else {
		eaPush(&topDirs, "maps");
	}
	eaPush(&exts, ".clayer");
	eaPush(&exts, ".tlayer");
	eaPush(&exts, ".layer");
	getDirectoryName(startDir);
	browser = ui_FileBrowserCreateEx("Import Layer", "Import", UIBrowseExisting, UIBrowseFiles, true,
									 topDirs, startDir, NULL, exts, NULL, NULL, wleUIImportLayerDialogOkClicked,
									 NULL, NULL, NULL, NULL, false);
	elUICenterWindow(browser);
	ui_WindowShow(browser);
	eaDestroy(&topDirs);
	eaDestroy(&exts);
}

/********************
* DELETE LAYER DIALOG
********************/
static bool wleUIDeleteLayerDialogCreateConfirm(UIDialog *dialog, UIDialogButton button, ZoneMapLayer *layer)
{
	if (button == kUIDialogButton_Ok)
		wleOpDeleteLayer(layerGetFilename(layer));
	return true;
}

/******
* This function brings up a prompt to confirm that the user wants to delete a layer from the map and
* that the user is aware that the map will be saved.  If the user clicks OK, then the deletion operation
* is executed.
* PARAMS:
*   layer - ZoneMapLayer being deleted
******/
void wleUIDeleteLayerDialogCreate(ZoneMapLayer *layer)
{
	UIDialog *dialog = ui_DialogCreateEx("WARNING",
		"This operation will disassociate an entire layer from the map. Are you sure you want to continue?",
		wleUIDeleteLayerDialogCreateConfirm, layer, NULL,
		"Cancel", kUIDialogButton_Cancel,
		"OK", kUIDialogButton_Ok,
		NULL);
	ui_WindowShow(UI_WINDOW(dialog));
	ui_SetFocus(UI_WIDGET(dialog)->children[2]);
}

/*******************************
* SAVE TO OBJECT LIBRARY DIALOG
********************************/

typedef struct wleUISaveToLibraryUI
{
	UIWindow *window;
	UITextEntry *name_entry;
	UICheckButton *core_check;

	wleUIFolderSelectUI folder_ui;
	GroupTracker **trackers;
} wleUISaveToLibraryUI;

static wleUISaveToLibraryUI g_SaveToLibUI = {0};

static void wleUISaveToLibNameOk(UIWidget *unused, wleUISaveToLibraryUI *ui)
{
	int i;
	const char **names = NULL;
	TrackerHandle **handles = NULL;
	UITreeNode *selected_node = ui_TreeGetSelected(ui->folder_ui.dest_tree);
	char dest_file[MAX_PATH];
	bool is_core = ui_CheckButtonGetState(ui->core_check);
	
	if (!selected_node || !selected_node->contents)
	{
		eaDestroy(&ui->trackers);
		return;
	}

	if (!stricmp(selected_node->contents, "object_library"))
	{
		emStatusPrintf("Cannot save object to root! Pick a subdirectory to save to.");
		eaDestroy(&ui->trackers);
		return;
	}

	if (eaSize(&ui->trackers) == 1)
	{
		const char *newName = ui_TextEntryGetText(ui->name_entry);
		if (!resIsValidName(newName))
		{
			emStatusPrintf("Invalid object name: %s", newName);
			eaDestroy(&ui->trackers);
			return;
		}
		newName = allocAddString(newName);
		eaPush(&names, newName);
		eaPush(&handles, trackerHandleCreate(ui->trackers[0]));
	}
	else
	{
		for (i = 0; i < eaSize(&ui->trackers); i++)
		{
			eaPush(&names, ui->trackers[i]->def->name_str);
			eaPush(&handles, trackerHandleCreate(ui->trackers[i]));
		}
	}

	if (eaSize(&names) == 0)
	{
		eaDestroy(&ui->trackers);
		return;
	}

	for (i = 0; i < eaSize(&names); i++)
	{
		if (!groupLibIsValidGroupName(objectLibraryGetEditingDefLib(), names[i], 0) ||
			!groupLibIsValidGroupName(objectLibraryGetDefLib(), names[i], 0))
		{
			emStatusPrintf("Object library piece already exists with name %s.", names[i]);
			return;
		}
	}

	assert(eaSize(&names) == eaSize(&handles));
	for (i = 0; i < eaSize(&handles); i++)
	{
		sprintf(dest_file, "%s/%s/%s.objlib", (is_core && fileCoreDataDir()) ? fileCoreDataDir() : fileDataDir(), (char*)selected_node->contents, names[i]);

		if (!objectLibrarySetFileEditable(dest_file))
		{
			emStatusPrintf("Could not check out file %s! Please pick another file.", dest_file);
			eaDestroy(&handles);
			eaDestroy(&names);
			eaDestroy(&ui->trackers);
			return;
		}

		wleOpSaveToLib(handles[i], dest_file, names[i]);
	}

	eaDestroy(&handles);
	eaDestroy(&names);
	eaDestroy(&ui->trackers);

	EditorPrefStoreString(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SaveToObjLibFolder", (char*)selected_node->contents);

	ui_WidgetQueueFree(UI_WIDGET(ui->window));
}

static bool wleUISaveToLibInputFunc(UIWindow *window, KeyInput *key)
{
	if (key->type == KIT_EditKey)
	{
		if (key->scancode == INP_RETURN)
		{
			wleUISaveToLibNameOk(NULL, &g_SaveToLibUI);
			return true;
		}
	}
	return ui_WindowInput(window, key);
}

static bool wleUISaveToLibTextInputFunc(UITextEntry *entry, KeyInput *key)
{
	if (key->type == KIT_EditKey)
	{
		if (key->scancode == INP_RETURN)
		{
			ui_SetFocus(g_SaveToLibUI.window);
			return true;
		}
	}
	return ui_TextEntryInput(entry, key);
}

void wleUISaveToLibDialogCreate(GroupTracker **trackers)
{
	int i;
	UILabel *label;
	UIButton *button;
	const char *defaultName = (eaSize(&trackers) == 1) ? trackers[0]->def->name_str : NULL;
	const char *last_dir;

	eaCopy(&g_SaveToLibUI.trackers, &trackers);

	g_SaveToLibUI.window = ui_WindowCreate("Enter New Name", 0, 0, 0, 0);
	ui_WindowSetCloseCallback(g_SaveToLibUI.window, elUIWindowClose, g_SaveToLibUI.window);
	editorUIState->currModalWin = g_SaveToLibUI.window;

	label = ui_LabelCreate("New group name:", 5, 5);
	ui_WindowAddChild(g_SaveToLibUI.window, label);
	g_SaveToLibUI.name_entry = ui_TextEntryCreate(defaultName ? defaultName : "", 0, label->widget.y);
	g_SaveToLibUI.name_entry->widget.width = 1;
	g_SaveToLibUI.name_entry->widget.widthUnit = UIUnitPercentage;
	g_SaveToLibUI.name_entry->widget.leftPad = elUINextX(label) + 5;
	g_SaveToLibUI.name_entry->widget.inputF = wleUISaveToLibTextInputFunc;
	ui_SetActive(UI_WIDGET(g_SaveToLibUI.name_entry), (defaultName != NULL));
	ui_WindowAddChild(g_SaveToLibUI.window, g_SaveToLibUI.name_entry);

	label = ui_LabelCreate("Location:", 5, elUINextY(g_SaveToLibUI.name_entry));
	ui_WindowAddChild(g_SaveToLibUI.window, label);

	g_SaveToLibUI.core_check = ui_CheckButtonCreate(elUINextX(label)+15, label->widget.y, "Save to Core", false);
	ui_SetActive(UI_WIDGET(g_SaveToLibUI.core_check), (fileCoreDataDir() != NULL));
	ui_WindowAddChild(g_SaveToLibUI.window, g_SaveToLibUI.core_check);

	g_SaveToLibUI.folder_ui.dest_tree = ui_TreeCreate(5, elUINextY(g_SaveToLibUI.core_check), 1, 400);
	g_SaveToLibUI.folder_ui.dest_tree->widget.widthUnit = UIUnitPercentage;
	g_SaveToLibUI.folder_ui.dest_tree->root.fillF = wleUIFolderRootFill;
	g_SaveToLibUI.folder_ui.dest_tree->root.fillData = "object_library";
	ui_TreeNodeExpand(&g_SaveToLibUI.folder_ui.dest_tree->root);

	last_dir = EditorPrefGetString(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SaveToObjLibFolder", "");
	if (last_dir && last_dir[0])
	{
		wleUIExpandFolderTree(&g_SaveToLibUI.folder_ui.dest_tree->root, last_dir);
	}
	else
	{
		for (i = 0; i < eaSize(&g_SaveToLibUI.folder_ui.dest_tree->root.children); i++)
			ui_TreeNodeExpand(g_SaveToLibUI.folder_ui.dest_tree->root.children[i]);
	}

	ui_WindowAddChild(g_SaveToLibUI.window, g_SaveToLibUI.folder_ui.dest_tree);

	button = ui_ButtonCreate("New Folder...", 5, 5, wleUIFolderCreateNew, &g_SaveToLibUI.folder_ui);
	button->widget.width = 100;
	button->widget.offsetFrom = UIBottomLeft;
	ui_WindowAddChild(g_SaveToLibUI.window, button);

	button = elUIAddCancelOkButtons(g_SaveToLibUI.window, NULL, NULL, wleUISaveToLibNameOk, &g_SaveToLibUI);
	ui_WindowSetDimensions(g_SaveToLibUI.window, elUINextX(label) + 250, elUINextY(g_SaveToLibUI.folder_ui.dest_tree) + elUINextY(button) + 5, elUINextX(label) + 200, elUINextY(button) + elUINextY(button) + 5);
	elUICenterWindow(g_SaveToLibUI.window);
	ui_WindowSetModal(g_SaveToLibUI.window, true);
	ui_WindowShow(g_SaveToLibUI.window);
	g_SaveToLibUI.window->widget.inputF = wleUISaveToLibInputFunc;
	if (defaultName)
		ui_SetFocus(g_SaveToLibUI.name_entry);
}

static void wleUIDeleteFromLibConfirmOk(UIButton *button, GroupDef *def)
{
	ui_WidgetQueueFree((UIWidget*) editorUIState->currModalWin);
	wleOpDeleteFromLib(def->name_uid);
}

void wleUIDeleteFromLibConfirm(GroupDef *def)
{
	int textWidth;
	UIWindow *window;
	char dialog[1024];
	sprintf(dialog, "Are you sure you want to delete \"%s\" from the object library\?", def->name_str);
	textWidth = gfxfont_StringWidth(&g_font_Sans, 1, 1, dialog);
	window = ui_WindowCreate("Are you sure?", 0, 0, textWidth + 20, 90);
	elUICenterWindow(window);
	ui_WindowAddChild(window, ui_LabelCreate(dialog, 10, 10));
	ui_WindowAddChild(window, ui_LabelCreate("This operation cannot be undone.", 10, 25));
	elUIAddCancelOkButtons(window, NULL, NULL, wleUIDeleteFromLibConfirmOk, def);
	editorUIState->currModalWin = window;
	ui_WindowSetModal(window, true);
	ui_WindowShow(window);
}

static bool wleUIDeleteFromLibErrorClose(UIWidget *unused, UIList *defList)
{
	eaDestroy(defList->peaModel);
	free(defList->peaModel);
	defList->peaModel = NULL;
	return true;
}

void wleUIDeleteFromLibError(GroupDef *refDef, GroupDef **containingDefs)
{
	UIWindow *window;
	UILabel *label;
	UIList *list;
	int i;
	char text[1024];
	char ***listModel = calloc(1, sizeof(char***));

	// create the UI
	sprintf(text, "Groups containing \"%s\"", refDef->name_str);
	window = ui_WindowCreate(text, 0, 0, 600, 200);
	elUICenterWindow(window);
	editorUIState->currModalWin = window;
	ui_WindowSetModal(window, true);
	sprintf(text, "The following definitions have \"%s\" as a child.", refDef->name_str);
	label = ui_LabelCreate(text, 5, 5);
	ui_WindowAddChild(window, label);
	sprintf(text, "Please remove these references before deleting \"%s\" from the object library.", refDef->name_str);
	label = ui_LabelCreate(text, 5, 20);
	ui_WindowAddChild(window, label);
	for (i = 0; i < eaSize(&containingDefs); i++)
		eaPush(listModel, (char*)containingDefs[i]->name_str);
	list = ui_ListCreate(NULL, listModel, 15);
	ui_WidgetSetDimensionsEx((UIWidget*) list, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx((UIWidget*) list, 5, 5, 40, 5);
	ui_ListAppendColumn(list, ui_ListColumnCreate(UIListTextCallback, "Containing Definitions", (intptr_t) elUIListTextDisplay, NULL));
	ui_WindowAddChild(window, list);
	ui_WindowSetCloseCallback(window, wleUIDeleteFromLibErrorClose, list);
	ui_WindowShow(window);
}

// HELP MENU CALLBACKS
void worldEditorOpenDocumentation(UIMenuItem *menuItem, UserData *stuff)
{
	openCrypticWikiPage("Core/World+Editor");
}

void showVersionInfo(UIMenuItem *menuItem, UserData *stuff)
{
	ui_ModalDialog("Version Info", "Cryptic World Editor\nv0000.00.does-this-matter?\nCopyright 2006 Cryptic Studios.\nAll rights reserved.\nIncluding the right to put easter eggs here...", ColorBlack, UIOk);
}

static bool wleUIResourcesLoaded()
{
	DisplayMessage *zmapMessage = zmapInfoGetDisplayNameMessage(NULL);
	const char *refString = NULL;

	// activate display name widget
	if (zmapMessage)
	{
		if (zmapMessage->pEditorCopy)
			refString = zmapMessage->pEditorCopy->pcMessageKey;
		else
			refString = REF_STRING_FROM_HANDLE(zmapMessage->hMessage);
		if(refString && !resIsEditingVersionAvailable(gMessageDict, refString))
			return false;
	}

	{
		refString = zmapInfoGetRewardTableString(NULL);
		if(refString)
		{
			ResourceInfo *resInfo = resGetInfo("RewardTable", refString);
			if(!resInfo)
				return false;
		}
	}

	return true;
}

void wleUIOncePerFrame(void)
{
	static bool resourcesLoaded = false;

	if (!editorUIState)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (resourcesLoaded != wleUIResourcesLoaded())
	{
		resourcesLoaded = !resourcesLoaded;
		wleUIMapPropertiesRefresh();
	}

	wleAEOncePerFrame();

	PERFINFO_AUTO_STOP_FUNC();
}

/********************
* INFO WINDOW
********************/
static int getModelSysBytes(U32* sys, StashElement element)
{
	Model *m = stashElementGetKey(element);
	ModelLOD *high_lod = modelLoadLOD(m, 0);
	if (!high_lod)
		high_lod = modelLoadLOD(m, 1);
	if (high_lod)
		*sys += modelLODGetBytesSystem(high_lod);
	return 1;
}

static int getModelVidBytes(U32* vid, StashElement element)
{
	Model *m = stashElementGetKey(element);
	ModelLOD *high_lod = modelLoadLOD(m, 0);
	if (!high_lod)
		high_lod = modelLoadLOD(m, 1);
	if (high_lod)
		*vid += modelLODGetBytesUnpacked(high_lod);
	return 1;
}

static U32 getSelectedGroupModelBytes(U32 *sys, U32 *vid)
{
	StashTable modelHash = stashTableCreateAddress(256);
	GroupTracker *tracker, **trackers = NULL;
	TrackerHandle **handles = NULL;
	U32 count;
	int i;

	wleSelectionGetTrackerHandles(&handles);
	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *selected = trackerFromTrackerHandle(handles[i]);
		if (selected)
			eaPush(&trackers, selected);
	}
	eaDestroy(&handles);

	while (tracker = eaPop(&trackers))
	{
		if (tracker->def && tracker->def->model)
			stashAddressAddPointer(modelHash, tracker->def->model, tracker->def->model, false);
		for (i = 0; i < tracker->child_count; ++i)
			eaPush(&trackers, tracker->children[i]);
	}

	*sys = 0;
	*vid = 0;
	stashForEachElementEx(modelHash, getModelSysBytes, sys);
	stashForEachElementEx(modelHash, getModelVidBytes, vid);
	count = stashGetCount(modelHash);

	stashTableDestroy(modelHash);
	eaDestroy(&trackers);

	return count;
}

static U32 getSelectedGroupModelVertCount(U32 *model_count, U32 *vertex_color_memory)
{
	GroupTracker *tracker, **trackers = NULL;
	TrackerHandle **handles = NULL;
	U32 count = 0;
	int i;

	wleSelectionGetTrackerHandles(&handles);
	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *selected = trackerFromTrackerHandle(handles[i]);
		if (selected)
			eaPush(&trackers, selected);
	}
	eaDestroy(&handles);

	*model_count = 0;

	while (tracker = eaPop(&trackers))
	{
		if (tracker->def && tracker->def->model)
		{
			ModelLOD *model_lod = modelLoadLOD(tracker->def->model, 0); 
			ModelLODData *data = SAFE_MEMBER(model_lod, data);
			*model_count += 1;
			count += SAFE_MEMBER(data, vert_count);
		}
		for (i = 0; i < tracker->child_count; ++i)
			eaPush(&trackers, tracker->children[i]);
	}

	*vertex_color_memory = count * sizeof(U32) + *model_count * 2 * sizeof(F32);

	eaDestroy(&trackers);

	return count;
}

static void wleUIInfoWinModelName(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	strdup_alloca(str, editState.rayCollideInfo.model ? editState.rayCollideInfo.model->name : "N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
}

static void wleUIInfoWinModelPosition(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	WorldCellEntry *entry;
	
	if (editState.rayCollideInfo.entry)
		entry = &editState.rayCollideInfo.entry->base_entry;
	else if (editState.rayCollideInfo.volumeEntry)
		entry = &editState.rayCollideInfo.volumeEntry->base_entry;
	else
		entry = NULL;

	if (entry)
		str = strdupf("(%.2f, %.2f, %.2f)", entry->bounds.world_matrix[3][0], entry->bounds.world_matrix[3][1], entry->bounds.world_matrix[3][2]);
	else
		str = strdup("N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinModelPYR(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	WorldCellEntry *entry;

	if (editState.rayCollideInfo.entry)
		entry = &editState.rayCollideInfo.entry->base_entry;
	else if (editState.rayCollideInfo.volumeEntry)
		entry = &editState.rayCollideInfo.volumeEntry->base_entry;
	else
		entry = NULL;

	if (entry)
	{
		Vec3 pyr;

		getMat3YPR(entry->bounds.world_matrix, pyr);
		str = strdupf("(%.2f, %.2f, %.2f)", radToDeg(pyr[0]), radToDeg(pyr[1]), radToDeg(pyr[2]));
	}
	else
		str = strdup("N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

void wleUIInfoWinMaterial(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	strdup_alloca(str, editState.rayCollideInfo.mat ? editState.rayCollideInfo.mat->material_name : "N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
}

static void wleUIInfoWinMaterialMemory(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	if (editState.rayCollideInfo.mat)
	{
		U32 total, shared;

		gfxMaterialsGetMemoryUsage(editState.rayCollideInfo.mat, &total, &shared);
		strdup_alloca(str, friendlyBytes(total - shared));
	}
	else
		strdup_alloca(str, "N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
}

static void wleUIInfoWinPhysProp(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	if (editState.rayCollideInfo.physProp)
		str = strdupf("%s", editState.rayCollideInfo.physProp->name_key);
	else
		strdup_alloca(str, "N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
}	

static void wleUIInfoWinModelMemory(const char *indexed_name, EMInfoWinText ***text_lines)
{
	ModelLOD *modelLOD = NULL;
	char *str;

	modelLOD = modelLoadLOD(editState.rayCollideInfo.model, 0);
	if (modelLOD)
		strdup_alloca(str, friendlyBytes(modelLODGetBytesTotal(modelLOD)))
	else
		strdup_alloca(str, "N/A")
	eaPush(text_lines, emInfoWinCreateTextLine(str));
}

static void wleUIInfoWinTriangleCount(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	if (editState.rayCollideInfo.model)
		str = strdupf("%i", SAFE_MEMBER2(editState.rayCollideInfo.model, header, tri_count));
	else
		str = strdup("N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinSelectionTriCount(const char *indexed_name, EMInfoWinText ***text_lines)
{
	GroupTracker *tracker, **trackers = NULL;
	TrackerHandle **handles = NULL;
	U32 triCount = 0, selectionCount = 0;
	char *str;
	int i;

	wleSelectionGetTrackerHandles(&handles);
	selectionCount = eaSize(&handles);
	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *selected = trackerFromTrackerHandle(handles[i]);
		if (selected)
			eaPush(&trackers, selected);
	}
	eaDestroy(&handles);

	// go through all trackers and children to count model triangles
	while (tracker = eaPop(&trackers))
	{
		if (tracker->def && tracker->def->model)
			triCount += SAFE_MEMBER2(tracker->def->model, header, tri_count);
		for (i = 0; i < tracker->child_count; ++i)
			eaPush(&trackers, tracker->children[i]);
	}

	eaDestroy(&trackers);
	if (selectionCount > 0)
		str = strdupf("%i", triCount);
	else
		str = strdup("N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinVertexCount(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	if (editState.rayCollideInfo.model) {
		ModelLOD *model_lod = modelLoadLOD(editState.rayCollideInfo.model, 0);
		str = strdupf("%i", SAFE_MEMBER2(model_lod, data, vert_count));
	} else
		str = strdup("N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinSubobjectCount(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	if (editState.rayCollideInfo.model) {
		ModelLOD *model_lod = modelLoadLOD(editState.rayCollideInfo.model, 0);
		str = strdupf("%i", SAFE_MEMBER2(model_lod, data, tex_count));
	} else
		str = strdup("N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinWidget(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str;
	switch(editState.transformMode)
	{
		xcase EditTranslateGizmo:
			str = "Translate";
		xcase EditRotateGizmo:
			str = "Rotate";
		xcase EditScaleMin:
			str = "Inner Radius";
		xcase EditScale:
			str = "Outer Radius";
		xcase EditMinCone:
			str = "Inner Angle(s)";
		xcase EditMaxCone:
			str = "Outer Angle(s)";
		xdefault:
			str = "Disabled";
	}
	eaPush(text_lines, emInfoWinCreateTextLine(str));
}

static void wleUIInfoWinRotationSnapAngle(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str = strdupf("%i deg", GizmoGetSnapAngle(RotateGizmoGetSnapResolution(edObjHarnessGetRotGizmo())));
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinTranslationSnapWidth(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str = strdupf("%.2f ft", GizmoGetSnapWidth(TranslateGizmoGetSnapResolution(edObjHarnessGetTransGizmo())));
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinSelectionCount(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char *str = strdupf("%i", edObjSelectionGetCount(EDTYPE_NONE));
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinSelectionMemory(const char *indexed_name, EMInfoWinText ***text_lines)
{
	U32 sys, vid;
	U32 modelCount = getSelectedGroupModelBytes(&sys, &vid);
	char *str = strdupf("%d unique models", modelCount);
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
	strdup_alloca(str, friendlyBytes(sys + vid));
	eaPush(text_lines, emInfoWinCreateTextLine(str));
}

static void wleUIInfoWinSelectionVerts(const char *indexed_name, EMInfoWinText ***text_lines)
{
	U32 modelCount, mem;
	U32 vertexCount = getSelectedGroupModelVertCount(&modelCount, &mem);
	char *str = strdupf("%i models", modelCount);
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
	str = strdupf("%d verts", vertexCount);
	eaPush(text_lines, emInfoWinCreateTextLine(str));
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*str'"
	free(str);
	str = strdupf("%s vertex color memory", friendlyBytes(mem));
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinWorldTime(const char *indexed_name, EMInfoWinText ***text_lines)
{
	float time = wlTimeGet();
	int hrs = ((int) time) % 12;
	char *str;
	if (hrs == 0)
		hrs = 12;
	str = strdupf("%02i:%02i %s", hrs, (int)((time - (int) time) * 60), (time >= 12 ? "PM" : "AM"));
	eaPush(text_lines, emInfoWinCreateTextLine(str));
	free(str);
}

static void wleUIInfoWinLayer(const char *indexed_name, EMInfoWinText ***text_lines)
{
	ZoneMapLayer *layer = heightMapGetLayer(editState.rayCollideInfo.heightMap);
	char *str;

	if (!layer)
	{
		WorldCellEntry *entry;

		if (editState.rayCollideInfo.entry)
			entry = &editState.rayCollideInfo.entry->base_entry;
		else if (editState.rayCollideInfo.volumeEntry)
			entry = &editState.rayCollideInfo.volumeEntry->base_entry;
		else
			entry = NULL;

		if (entry)
			layer = worldEntryGetLayer(entry);
	}

	strdup_alloca(str, layer ? layerGetName(layer) : "N/A");
	eaPush(text_lines, emInfoWinCreateTextLine(str));
}

static void wleUIInfoWinCamDist(const char* indexed_name, EMInfoWinText ***text_lines)
{
	char *str = NULL;

	estrClear(&str);
	if(editState.rayCollideInfo.entry)
	{
		Vec3 camPos;
		F32 dist;
		gfxGetActiveCameraPos(camPos);
		dist = distance3(editState.rayCollideInfo.entry->base_entry.bounds.world_mid, camPos);

		estrPrintf(&str, "%.2f", dist);
	}
	else
		estrPrintf(&str, "N/A");

	eaPush(text_lines, emInfoWinCreateTextLine(str));
}

void wleUIRegisterInfoWinEntries(EMEditor *editor)
{
	emInfoWinEntryRegister(editor, "name", "Model Name", wleUIInfoWinModelName);
	emInfoWinEntryRegister(editor, "pos", "Model Position", wleUIInfoWinModelPosition);
	emInfoWinEntryRegister(editor, "rot", "Model PYR", wleUIInfoWinModelPYR);
	emInfoWinEntryRegister(editor, "mat", "Material", wleUIInfoWinMaterial);
	emInfoWinEntryRegister(editor, "matbytes", "Material Memory", wleUIInfoWinMaterialMemory);
	emInfoWinEntryRegister(editor, "physprop", "Physical Property", wleUIInfoWinPhysProp);
	emInfoWinEntryRegister(editor, "modelbytes", "Model Memory", wleUIInfoWinModelMemory);
	emInfoWinEntryRegister(editor, "tricount", "Triangle Count", wleUIInfoWinTriangleCount);
	emInfoWinEntryRegister(editor, "selecttricount", "Selection Triangle Count", wleUIInfoWinSelectionTriCount);
	emInfoWinEntryRegister(editor, "vertcount", "Vertex Count", wleUIInfoWinVertexCount);
	emInfoWinEntryRegister(editor, "subobjcount", "Subobject Count", wleUIInfoWinSubobjectCount);
	emInfoWinEntryRegister(editor, "widget", "Widget", wleUIInfoWinWidget);
	emInfoWinEntryRegister(editor, "rotsnapres", "Rotation Snap Angle", wleUIInfoWinRotationSnapAngle);
	emInfoWinEntryRegister(editor, "transsnapgrid", "Translation Snap Width", wleUIInfoWinTranslationSnapWidth);
	emInfoWinEntryRegister(editor, "selectcount", "Selection Count", wleUIInfoWinSelectionCount);
	emInfoWinEntryRegister(editor, "selectedmodelbytes", "Selection Memory", wleUIInfoWinSelectionMemory);
	emInfoWinEntryRegister(editor, "selectedmodelverts", "Selection Verts", wleUIInfoWinSelectionVerts);
	emInfoWinEntryRegister(editor, "worldtime", "World Time", wleUIInfoWinWorldTime);
	emInfoWinEntryRegister(editor, "layer", "Layer", wleUIInfoWinLayer);
	emInfoWinEntryRegister(editor, "dist", "Cam Dist", wleUIInfoWinCamDist);
}

typedef struct WleUIUniqueNameRequiredWin
{
	WleUIUniqueNameOkCallback okCallback;
	void *okData;
	WleUIUniqueNameCancelCallback cancelCallback;
	void *cancelData;

	UIWindow *win;
	UITextEntry *entry;
} WleUIUniqueNameRequiredWin;

void wleUIUniqueNameRequiredClose(WleUIUniqueNameRequiredWin *ui)
{
	ui_WindowClose(ui->win);
	free(ui);
}

void wleUIUniqueNameRequiredCancel(UIWidget *unused, WleUIUniqueNameRequiredWin *ui)
{
	ui_WindowClose(ui->win);
	free(ui);
}

void wleUIUniqueNameRequiredOk(UIWidget *unused, WleUIUniqueNameRequiredWin *ui)
{
	const char *text = ui_TextEntryGetText(ui->entry);
	if (text && ui->okCallback(text, ui->okData))
		wleUIUniqueNameRequiredClose(ui);
}

void wleUIUniqueNameRequiredNameChanged(UITextEntry *entry, UIButton *button)
{
	const char *text = ui_TextEntryGetText(entry);
	ui_SetActive(UI_WIDGET(button), (text && text[0]));
}

void wleUIUniqueNameRequiredDialog(const char *prompt, const char *initialName, WleUIUniqueNameOkCallback okCallback, void *okData, WleUIUniqueNameCancelCallback cancelCallback, void *cancelData)
{
	WleUIUniqueNameRequiredWin *ui = calloc(1, sizeof(*ui));
	UIWindow *win;
	UILabel *label;
	UITextEntry *entry;
	UIButton *button;

	ui->okCallback = okCallback;
	ui->okData = okData;
	ui->cancelCallback = cancelCallback;
	ui->cancelData = cancelData;

	win = ui_WindowCreate("Enter unique name", 0, 0, 400, 0);
	ui_WindowSetClosable(win, false);
	ui->win = win;
	label = ui_LabelCreate(prompt, 0, 5);
	label->widget.width = 1;
	label->widget.widthUnit = UIUnitPercentage;
	label->widget.leftPad = label->widget.rightPad = 5;
	ui_LabelSetWordWrap(label, true);
	ui_WindowAddChild(win, label);
	label = ui_LabelCreate("Unique name", 5, elUINextY(label) + 5);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	entry->widget.leftPad = elUINextX(label) + 5;
	entry->widget.rightPad = 5;
	ui_WindowAddChild(win, entry);
	ui->entry = entry;
	button = elUIAddCancelOkButtons(win, wleUIUniqueNameRequiredCancel, ui, wleUIUniqueNameRequiredOk, ui);
	ui_TextEntrySetChangedCallback(entry, wleUIUniqueNameRequiredNameChanged, button);
	ui_TextEntrySetEnterCallback(entry, wleUIUniqueNameRequiredOk, ui);
	wleUIUniqueNameRequiredNameChanged(entry, button);
	win->widget.height = elUINextY(button) + elUINextY(label) + 5;
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
	ui_SetFocus(entry);
}

/********************
* LAYER UI'S
********************/
void wleUILayerContextMenu(EditorObject *edObj, UIMenuItem ***outItems)
{
	ZoneMapLayer *layer;
	GroupTracker *tracker;

	assert(edObj->type->objType == EDTYPE_LAYER);
	layer = (ZoneMapLayer*) edObj->obj;
	tracker = layerGetTracker(layer);
	if (tracker)
		eaPush(outItems, ui_MenuItemCreate("Set Default Parent", UIMenuCallback, wleMenuSetDefaultParentTracker, tracker, NULL));
}

/********************
* Other
********************/

bool wleDeleteOusideVolumeRecursive(GroupTracker *pTracker, GroupTracker *pParentTracker, Vec3 vExtents[2], Mat4 mVolMat, TrackerHandle ***pppToDelete)
{
	int i;
	GroupDef *pDef = SAFE_MEMBER(pTracker, def);
	Mat4 mMat;
	Vec3 vMin, vMax;

	if(!pTracker || !pDef)
		return false;

	trackerGetMat(pTracker, mMat);
	copyVec3(pDef->bounds.min, vMin);
	copyVec3(pDef->bounds.max, vMax);
	if(pParentTracker && !orientBoxBoxCollision(vMin, vMax, mMat, vExtents[0], vExtents[1], mVolMat)) {
		//Remove from the world
		TrackerHandle *pHandle = trackerHandleCreate(pTracker);
		eaPush(pppToDelete, pHandle);
		if(!wleTrackerIsEditable(trackerHandleFromTracker(pParentTracker), false, false, false)) {
			Errorf("Could not remove object because it was not editable");
			return false;
		}
	} else if(!groupIsObjLib(pDef)) {
		//Otherwise recurse
		for ( i=0; i < pTracker->child_count; i++ ) {
			if(!wleDeleteOusideVolumeRecursive(pTracker->children[i], pTracker, vExtents, mVolMat, pppToDelete))
				return false;
		}
	}
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.DeleteOutsideVolume");
void wleCmdDeleteOusideVolume(void)
{
	int i;
	EditorObject **ppObjects = NULL;
	TrackerHandle **ppToDelete = NULL;
	EditUndoStack *pStack = edObjGetUndoStack();
	EditorObject *pObject;
	GroupTracker *pVolTracker;
	GroupDef *pGroup;
	Vec3 vExtents[2];
	Mat4 mMat;
	ZoneMap *pZMap;

	wleAEGetSelectedObjects(&ppObjects);

	if(eaSize(&ppObjects) != 1) {
		Errorf("You must have exactly one object selected.");
		return;
	}

	pObject = ppObjects[0];

	if (pObject->type->objType != EDTYPE_TRACKER) {
		Errorf("The selected object must be of type tracker.");
		return;
	}

	pVolTracker = trackerFromTrackerHandle(pObject->obj);
	pGroup = SAFE_MEMBER(pVolTracker, def);

	if(!pGroup) {
		Errorf("Can not find group on the tracker.");
		return;
	}

	if (!pGroup->property_structs.volume || pGroup->property_structs.volume->eShape != GVS_Box) {
		Errorf("The selected object must be a box volume.");
		return;
	}
	copyVec3(pGroup->property_structs.volume->vBoxMin, vExtents[0]);
	copyVec3(pGroup->property_structs.volume->vBoxMax, vExtents[1]);
	trackerGetMat(pVolTracker, mMat);
	
	pZMap = worldGetActiveMap();
	if(!pZMap) {
		Errorf("No Active Zone Map.");
		return;
	}

	for ( i=0; i < eaSize(&pZMap->layers); i++ ) {
		ZoneMapLayer *pLayer = pZMap->layers[i];
		GroupTracker *pTracker = pLayer->grouptree.root_tracker;
		if(wleDeleteOusideVolumeRecursive(pTracker, NULL, vExtents, mMat, &ppToDelete))
			wleOpDelete(ppToDelete);
		eaDestroyEx(&ppToDelete, trackerHandleDestroy);
	}
}

/********************
* GENESIS UI
********************/

//Global data for genesis data
typedef struct WleGenesisUI
{
	EMPanel *panel;
	UIWidget** alwaysActiveGenesisWidgets;
	UIWidget** genesisWidgets;
	UIWidget** backupGenesisWidgets;
	UIComboBox *edit_mode_combo;
	UIButton *edit_button;
} WleGenesisUI;
static WleGenesisUI WleGlobalGenesisUI = {0};

void wleGenesisUITick(void)
{
	int isGenesis = zmapInfoHasGenesisData(NULL);
	bool isBackupGenesis = zmapInfoHasBackupGenesisData(NULL);
	GenesisEditType zmapEditType;
	GenesisEditType comboEditType;
	bool editingGenesis;
	int it;
	
	if(!WleGlobalGenesisUI.panel)
		return;

	zmapEditType = zmapGetGenesisEditType(NULL);
	comboEditType = ui_ComboBoxGetSelectedEnum(WleGlobalGenesisUI.edit_mode_combo);
	if(zmapEditType != comboEditType)
		ui_ComboBoxSetSelectedEnum(WleGlobalGenesisUI.edit_mode_combo, zmapEditType);
	editingGenesis = ((zmapEditType == GENESIS_EDIT_EDITING) && isGenesis);

	if(editingGenesis)
		ui_ButtonSetText(WleGlobalGenesisUI.edit_button, "Edit Map Description");
	else
		ui_ButtonSetText(WleGlobalGenesisUI.edit_button, "View Map Description");

	GMDSetEmbeddedMapDescEnabled(editingGenesis);

	for(it = 0; it != eaSize(&WleGlobalGenesisUI.alwaysActiveGenesisWidgets); ++it) {
		ui_SetActive(WleGlobalGenesisUI.alwaysActiveGenesisWidgets[it], isGenesis);
	}
	for(it = 0; it != eaSize(&WleGlobalGenesisUI.genesisWidgets); ++it) {
		ui_SetActive(WleGlobalGenesisUI.genesisWidgets[it], editingGenesis);
	}
	for(it = 0; it != eaSize(&WleGlobalGenesisUI.backupGenesisWidgets); ++it) {
		ui_SetActive(WleGlobalGenesisUI.backupGenesisWidgets[it], !isGenesis && isBackupGenesis );
	}
}

static UIWindow* genesisErrorWindow = NULL;
static UIExpanderGroup* genesisErrorExpanderGroup = NULL;
static UIExpander** genesisErrorExpanders = NULL;
static UILabel* genesisErrorHeader = NULL;
static GenesisRuntimeStatus genesisErrorStatus = { 0 };

static void wleGenesisCloseWindow( void* ignored1, void* ignored2 )
{
	ui_WindowHide( genesisErrorWindow );
}

static void wleGenesisCopyErrors( void* ignored1, void* ignored2 )
{
	char* copyAccum = NULL;
	estrStackCreate( &copyAccum );

	{
		int numWarningsAccum = 0;
		int numErrorsAccum = 0;
		int numFatalErrorsAccum = 0;
		
		int it;
		for( it = 0; it != eaSize( &genesisErrorStatus.stages ); ++it ) {
			GenesisRuntimeStage* stage = genesisErrorStatus.stages[ it ];
			int stageNumWarningsAccum = 0;
			int stageNumErrorsAccum = 0;
			int stageNumFatalErrorsAccum = 0;
			char* accum;
			int errorIt;
			
			estrStackCreate( &accum );
			
			// FATAL ERRORS ARE RED
			for( errorIt = 0; errorIt != eaSize( &stage->errors ); ++errorIt ) {
				if( stage->errors[ errorIt ]->type == GENESIS_FATAL_ERROR ) {
					char contextText[ 256 ];
					
					genesisErrorPrintContextStr( stage->errors[ errorIt ]->context, SAFESTR( contextText ));
					estrConcatf( &accum, "FATAL ERROR: %s%s%s\n",
								 contextText, (contextText[0] ? " -- " : ""),
								 stage->errors[ errorIt ]->message );
					++numFatalErrorsAccum;
					++stageNumFatalErrorsAccum;
				}
			}

			// ERRORS ARE BLACK
			for( errorIt = 0; errorIt != eaSize( &stage->errors ); ++errorIt ) {
				if( stage->errors[ errorIt ]->type == GENESIS_ERROR ) {
					char contextText[ 256 ];
					
					genesisErrorPrintContextStr( stage->errors[ errorIt ]->context, SAFESTR( contextText ));
					estrConcatf( &accum, "ERROR: %s%s%s\n",
								 contextText, (contextText[0] ? " -- " : ""),
								 stage->errors[ errorIt ]->message );
					++numErrorsAccum;
					++stageNumErrorsAccum;
				}
			}
			
			// WARNINGS ARE DARK ORANGE
			for( errorIt = 0; errorIt != eaSize( &stage->errors ); ++errorIt ) {
				if( stage->errors[ errorIt ]->type == GENESIS_WARNING ) {
					char contextText[ 256 ];
					
					genesisErrorPrintContextStr( stage->errors[ errorIt ]->context, SAFESTR( contextText ));
					estrConcatf( &accum, "WARNING: %s%s%s\n",
								 contextText, (contextText[0] ? " -- " : ""),
								 stage->errors[ errorIt ]->message );
					++numWarningsAccum;
					++stageNumWarningsAccum;
				}
			}

			if( stageNumFatalErrorsAccum + stageNumErrorsAccum + stageNumWarningsAccum == 0 ) {
				estrConcatf( &accum, "Success!\n" );
			}

			{
				char buffer[ 1024 ];
				sprintf( buffer, "\n* %s -- %d Errors, %d Warnings\n",
						 stage->name,
						 stageNumFatalErrorsAccum + stageNumErrorsAccum,
						 stageNumWarningsAccum );
				string_toupper( buffer );
				estrInsert( &accum, 0, buffer, (int)strlen( buffer ));
			}

			estrConcatString( &copyAccum, accum, (int)strlen( accum ));
			estrDestroy( &accum );
		}
		
		{
			char* buffer = NULL;
			estrStackCreate( &buffer );
			estrPrintf( &buffer, "%d ERRORS -- %d FATAL, %d ERROR, %d WARNING\n",
						numFatalErrorsAccum + numErrorsAccum + numWarningsAccum,
						numFatalErrorsAccum, numErrorsAccum, numWarningsAccum );
			estrConcatCharCount( &buffer, '-', estrLength( &buffer ) - 1 );
			estrConcatChar( &buffer, '\n' );
			estrInsert( &copyAccum, 0, buffer, estrLength( &buffer ));
			estrDestroy( &buffer );
		}
	}

	winCopyToClipboard( copyAccum );
	estrDestroy( &copyAccum );
}

static void genesisErrorValidationTextReflow( UISMFView* smfView, UIExpander* parent )
{
	ui_ExpanderSetHeight( parent, smfView->widget.height );
}

static void wleGenesisEnsureErrorLayout( void )
{
	#define GenesisDisplayErrorWidth 500
	#define GenesisDisplayErrorHeight 300

	if( genesisErrorWindow ) {
		return;
	}

	// Set up the error window
	genesisErrorWindow = ui_WindowCreate( "Genesis Generation Report", 0, 0, 600, 400 );
	ui_WindowSetDimensions( genesisErrorWindow, GenesisDisplayErrorWidth, GenesisDisplayErrorHeight, GenesisDisplayErrorWidth, GenesisDisplayErrorHeight );
	genesisErrorWindow->widget.priority = 3;
	elUICenterWindow( genesisErrorWindow );

	// Set up the expander group
	genesisErrorExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetDimensionsEx( UI_WIDGET( genesisErrorExpanderGroup ), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage );

	// Set up the scroll bar
	{
		UIScrollArea* scrollArea = ui_ScrollAreaCreate( 0, 25, GenesisDisplayErrorWidth, GenesisDisplayErrorHeight, 1, 1, false, true );
		ui_WidgetSetDimensionsEx( UI_WIDGET( scrollArea ), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetSetPaddingEx( UI_WIDGET( scrollArea ), 0, 0, 0, 8 );
		scrollArea->autosize = true;
			
		ui_ScrollAreaAddChild( scrollArea, genesisErrorExpanderGroup );
		ui_WindowAddChild( genesisErrorWindow, scrollArea );
	}

	// Set up the header
	genesisErrorHeader = ui_LabelCreate( "<NULL>", 0, 0 );
	ui_LabelSetFont( genesisErrorHeader, ui_StyleFontGet( "Default_Bold" ));
	ui_WindowAddChild( genesisErrorWindow, genesisErrorHeader );

	// Set up the copy button
	{
		UIButton* button = ui_ButtonCreate( "Copy", -1, -1, wleGenesisCopyErrors, NULL );
		ui_WidgetSetPositionEx( UI_WIDGET( button ), 0, 0, 0, 0, UITopRight );
		ui_WindowAddChild( genesisErrorWindow, button );
	}

	// Set up the close button
	{
		UIButton* button = ui_ButtonCreate( "Close", -1, -1, wleGenesisCloseWindow, NULL );
		ui_WidgetSetPositionEx( UI_WIDGET( button ), 0, 0, 0, 0, UIBottom );
		ui_WindowAddChild( genesisErrorWindow, button );
	}
}

void wleGenesisDisplayErrorDialog(GenesisRuntimeStatus *gen_status)
{
	wleGenesisEnsureErrorLayout();

	StructCopyAll( parse_GenesisRuntimeStatus, gen_status, &genesisErrorStatus );

	// Clear previous errors
	{
		int it;
		for( it = 0; it != eaSize( &genesisErrorExpanders ); ++it ) {
			ui_ExpanderGroupRemoveExpander( genesisErrorExpanderGroup, genesisErrorExpanders[ it ]);
			ui_WidgetQueueFree( UI_WIDGET( genesisErrorExpanders[ it ]));
		}
		eaClear( &genesisErrorExpanders );
	}

	// Add the stages
	{
		int numWarningsAccum = 0;
		int numErrorsAccum = 0;
		int numFatalErrorsAccum = 0;
		
		int it;
		for( it = 0; it != eaSize( &genesisErrorStatus.stages ); ++it ) {
			GenesisRuntimeStage* stage = genesisErrorStatus.stages[ it ];
			int stageNumWarningsAccum = 0;
			int stageNumErrorsAccum = 0;
			int stageNumFatalErrorsAccum = 0;
			char* accum;
			int errorIt;
			
			estrStackCreate( &accum );
			
			// FATAL ERRORS ARE RED
			estrConcatf( &accum, "<font color=Red>" );
			for( errorIt = 0; errorIt != eaSize( &stage->errors ); ++errorIt ) {
				if( stage->errors[ errorIt ]->type == GENESIS_FATAL_ERROR ) {
					char errorText[ 1024 ];
					genesisErrorPrint( stage->errors[ errorIt ], SAFESTR( errorText ));
					estrConcatf( &accum, "%s<br>", errorText );
					++numFatalErrorsAccum;
					++stageNumFatalErrorsAccum;
				}
			}
			estrConcatf( &accum, "</font>" );

			// ERRORS ARE BLACK
			estrConcatf( &accum, "<font color=Black>" );
			for( errorIt = 0; errorIt != eaSize( &stage->errors ); ++errorIt ) {
				if( stage->errors[ errorIt ]->type == GENESIS_ERROR ) {
					char errorText[ 1024 ];
					genesisErrorPrint( stage->errors[ errorIt ], SAFESTR( errorText ));
					estrConcatf( &accum, "%s<br>", errorText );
					++numErrorsAccum;
					++stageNumErrorsAccum;
				}
			}
			estrConcatf( &accum, "</font>" );
			
			// WARNINGS ARE DARK ORANGE
			estrConcatf( &accum, "<font color=#A65300>" );
			for( errorIt = 0; errorIt != eaSize( &stage->errors ); ++errorIt ) {
				if( stage->errors[ errorIt ]->type == GENESIS_WARNING ) {
					char errorText[ 1024 ];
					genesisErrorPrint( stage->errors[ errorIt ], SAFESTR( errorText ));
					estrConcatf( &accum, "%s<br>", errorText );
					++numWarningsAccum;
					++stageNumWarningsAccum;
				}
			}
			estrConcatf( &accum, "</font>" );

			if( stageNumFatalErrorsAccum + stageNumErrorsAccum + stageNumWarningsAccum == 0 ) {
				estrConcatf( &accum, "<font color=Green>Success!<br></font>" );
			}

			{
				UISMFView* smfView = ui_SMFViewCreate( 0, 0, 1, 1 );
				UIExpander* expander = ui_ExpanderCreate( "", 1 );
				
				ui_SMFViewSetText( smfView, accum, NULL );
				ui_SMFViewSetReflowCallback( smfView, genesisErrorValidationTextReflow, expander );
				ui_SMFViewReflow( smfView, GenesisDisplayErrorWidth );
				ui_WidgetSetWidthEx( UI_WIDGET( smfView ), 1, UIUnitPercentage );

				{
					char buffer[ 1024 ];
					sprintf( buffer, "%s -- %d Errors, %d Warnings",
							 stage->name,
							 stageNumFatalErrorsAccum + stageNumErrorsAccum,
							 stageNumWarningsAccum );
					ui_WidgetSetTextString( UI_WIDGET( expander ), buffer );
				}
				ui_ExpanderAddChild( expander, UI_WIDGET( smfView ));
				ui_ExpanderSetOpened( expander, true );
				eaPush( &genesisErrorExpanders, expander );
			}

			estrDestroy( &accum );
		}
		
		{
			char buffer[ 1024 ];
			sprintf( buffer, "%d Errors -- %d Fatal, %d Error, %d Warning",
					 numFatalErrorsAccum + numErrorsAccum + numWarningsAccum,
					 numFatalErrorsAccum, numErrorsAccum, numWarningsAccum );
			ui_LabelSetText( genesisErrorHeader, buffer );
		}
	}

	// Add all the created expander groups
	{
		int it;
		for( it = 0; it != eaSize( &genesisErrorExpanders ); ++it ) {
			ui_ExpanderGroupAddExpander( genesisErrorExpanderGroup, genesisErrorExpanders[ it ]);
		}
	}

	ui_WindowPresent( genesisErrorWindow );
}

void wleGenesisUIImport(const char *path, const char *filename, void *unused)
{
	GenesisMapDescriptionFile *map_desc_file=NULL;
	char fullfilename[MAX_PATH];
	if(!path || !filename)
		return;

	edObjSelectionClear(EDTYPE_NONE);
	sprintf(fullfilename, "%s\\%s", path, filename);
	map_desc_file = StructCreate(parse_GenesisMapDescriptionFile);
	if(ParserReadTextFile(fullfilename, parse_GenesisMapDescriptionFile, map_desc_file, 0))
	{
		GenesisRuntimeStatus *gen_status;
		zmapInfoSetMapDesc(NULL, map_desc_file->map_desc);
		gen_status = wleOpGenesisRegenerate(true, true);
		if (gen_status) {
			wleGenesisDisplayErrorDialog(gen_status);
			StructDestroy(parse_GenesisRuntimeStatus, gen_status);
		}
		
		if (GMDEmbeddedMapDesc())
		{
			// close and reopen to refresh the map desc
			GMDCloseEmbeddedMapDesc();
			GMDOpenEmbeddedMapDesc(zmapInfoGetMapDesc(NULL), wleGenesisUIEditCompleteCB);
		}
	}
	else
	{
		emStatusPrintf("Import Failed: Unable to read file.");
	}
}

static bool wleGenesisUILockZmapImportCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success)
	{
		UIWindow *window;
		window = ui_FileBrowserCreate(	"Import MapDesc..", "Import", UIBrowseExisting, UIBrowseFiles, true,
										"genesis/MapDescriptions", "genesis/MapDescriptions", NULL, ".mapdesc",
										NULL, NULL, wleGenesisUIImport, NULL);
		ui_WindowShow(window);
	}

	return true;
}

void wleGenesisUIImportCB(void *unused, void *unused2)
{
	wleOpLockZoneMap(wleGenesisUILockZmapImportCB, NULL);
}

void wleGenesisUIExport(const char *path, const char *filename, void *unused)
{
	GenesisMapDescription *map_desc = zmapInfoGetMapDesc(NULL);
	GenesisMapDescriptionFile map_desc_file;
	char fullfilename[MAX_PATH];
	char basename[MAX_PATH];

	if(!map_desc || !path || !filename)
		return;

	map_desc_file.map_desc = StructClone(parse_GenesisMapDescription, map_desc);
	if( strlen(path) < strlen("Genesis/MapDescriptions/")) {
		map_desc_file.map_desc->scope = NULL;
	} else {
		map_desc_file.map_desc->scope = StructAllocString(path + strlen("Genesis/MapDescriptions/"));
	}
	
	getFileNameNoExt(basename, filename);
	map_desc_file.map_desc->name = StructAllocString(basename);

	sprintf(fullfilename, "%s\\%s", path, filename);
	ParserWriteTextFile(fullfilename, parse_GenesisMapDescriptionFile, &map_desc_file, 0, 0);

	StructDestroy(parse_GenesisMapDescription, map_desc_file.map_desc);
}

void wleGenesisUIExportCB(void *unused, void *unused2)
{
	UIWindow *window;
	window = ui_FileBrowserCreate(	"Export MapDesc..", "Export", UIBrowseNew, UIBrowseFiles, true,
									"genesis/MapDescriptions", "genesis/MapDescriptions", NULL,
									".mapdesc", NULL, NULL, wleGenesisUIExport, NULL);
	ui_WindowShow(window);
}

void wleGenesisUIEditCompleteCB(MapDescEditDoc *pDoc, GenesisMapDescription *map_desc, bool seed_layout, bool seed_detail)
{
	edObjSelectionClear(EDTYPE_NONE);
	if (map_desc) {
		GenesisRuntimeStatus *gen_status;
		zmapInfoSetMapDesc(NULL, StructClone(parse_GenesisMapDescription, map_desc));
		gen_status = wleOpGenesisRegenerate(seed_layout, seed_detail);
		if (gen_status) {
			wleGenesisDisplayErrorDialog(gen_status);
			StructDestroy(parse_GenesisRuntimeStatus, gen_status);
		}
	}
}

void wleGenesisUIEditOrViewCB(void *unused, void *unused2)
{
	GMDOpenEmbeddedMapDesc(zmapInfoGetMapDesc(NULL), wleGenesisUIEditCompleteCB);
}

static bool wleGenesisUILockZmapReseedCB(ZoneMapInfo *zminfo, bool success, int *reseed_detail)
{
	edObjSelectionClear(EDTYPE_NONE);
	if (success)
	{
		GenesisRuntimeStatus *gen_status;
		gen_status = wleOpGenesisRegenerate((*reseed_detail) == 0, true);
		if (gen_status) {
			wleGenesisDisplayErrorDialog(gen_status);
			StructDestroy(parse_GenesisRuntimeStatus, gen_status);
			emStatusPrintf("Reseeded Genesis Data");
		}
	}

	return true;
}

void wleGenesisUIReseedCB(void *unused, void *unused2)
{
	static int reseed_detail = 0;
	wleOpLockZoneMap(wleGenesisUILockZmapReseedCB, &reseed_detail);
}

void wleGenesisUIReseedDetailCB(void *unused, void *unused2)
{
	static int reseed_detail = 1;
	wleOpLockZoneMap(wleGenesisUILockZmapReseedCB, &reseed_detail);
}

static bool wleGenesisUILockZmapCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (!success)
		return false;
	
	if (ui_ModalDialog("Confirm Genesis Data Freezing",
					   "In order to be able to edit this map as a normal map, the "
					   "Genesis data must be frozen and the ZoneMap must be saved.  "
					   "This is potentially unsafe.\n"
					   "\n"
					   "Are you absolutely sure you want to freeze all Genesis data?",
					   ColorRed, UIYes | UINo) == UINo)
		return true;

	wleOpCommitGenesisData();

	return true;
}

void wleGenesisUICommitCB(void *unused, void *unused2)
{
	wleOpLockZoneMapEx(wleGenesisUILockZmapCommitCB, NULL, true);
}

static bool wleGenesisUILockZmapUnfreezeCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (!success)
		return false;
	
	if (ui_ModalDialog("Confirm Genesis Data Restore",
					   "To edit this map as a Genesis map, the Genesis data "
					   "must be un-frozen and the ZoneMap must be saved.  This "
					   "will throw away all edits you've made outside of Genesis.  "
					   "This is potentially unsafe.\n"
					   "\n"
					   "Are you absolutely sure you want to restore all Genesis data?",
					   ColorRed, UIYes | UINo) == UINo)
		return true;

	wleOpRestoreGenesisData();

	return true;
}

AUTO_COMMAND ACMD_NAME("genesisUnfreezeSingleMap");
void wleGenesisUIUnfreeze(void)
{
	if( genesisUnfreezeDisabled() ) {
		Alertf( "Genesis unfreezing is disabled." );
	} else {
		wleOpLockZoneMapEx(wleGenesisUILockZmapUnfreezeCB, NULL, true);
	}
}

void wleGenesisUIPreviewCB(void *unused, void *unused2)
{
	ZoneMap *zmap = worldGetActiveMap();

	if (!zmapGenesisDataLocked(zmap))
	{
		return;
	}

	// Bin the terrain
	genesisGenerateTerrain(PARTITION_CLIENT, zmap, false);
	terrainSaveRegionAtlases(zmapGetWorldRegionByName(zmap, "Default"));
	// Reload the bins
	layerReloadTerrainBins(zmap, zmapGetLayer(zmap, 0));
	
	// Send the latest data to the server for previewing
	zmapSetPreviewFlag(NULL);
}

void wleGenesisSelectEditType(UIComboBox *combo, int edit_type, void *userdata)
{
	if(edit_type == GENESIS_EDIT_STREAMING)
	{
		EMEditorDoc *world_doc;

		//Ensure changes are applied
		if(GMDEmbeddedMapDescHasChanges())
		{
			Alertf(	"You have not applied your changes in your Map Description.  "
					"You must apply or cancel your changes before moving to Game Mode." );
			return;
		}

		//Make sure map is saved
		world_doc = wleGetWorldEditorDoc();
		if(world_doc && !world_doc->saved)
		{
			if (ui_ModalDialog("Genesis Return To Game Mode",
				"You have unsaved changes.  "
				"Moving to game mode will revert your changes.\n"
				"\n"
				"Are you sure you want to continue?",
				ColorBlack, UIYes | UINo) == UINo)
				return;		
		}
		
		//Close the Window
		GMDCloseEmbeddedMapDesc();

		//Set zmap to saved
		zmapInfoSetSaved(NULL);
	}
	wleOpGenesisSetEditType(edit_type);
}

void wleGenesisSelectViewType(UIComboBox *combo, int view_type, void *userdata)
{
	zmapSetGenesisViewType(NULL, view_type);
}

StaticDefineInt GenesisViewTypeUIEnum[] = {
	DEFINE_INT
	{ "Nodes (Fastest)", GENESIS_VIEW_NODES },
	{ "Whitebox", GENESIS_VIEW_WHITEBOX },
	{ "No Terrain Detail", GENESIS_VIEW_NODETAIL },
	{ "Full (Slowest)", GENESIS_VIEW_FULL },
	DEFINE_END
};

void wleGenesisUICreate(EMEditorDoc *doc)
{
	UIButton *button;
	UIComboBox *combo;
	F32 y = 10;

	//Create Panel and add to the list
	WleGlobalGenesisUI.panel = emPanelCreate("Map", "Genesis", 0);
	eaPush(&doc->em_panels, WleGlobalGenesisUI.panel);

	combo = ui_ComboBoxCreateWithEnum(10, y, 235, GenesisEditTypeEnum, wleGenesisSelectEditType, NULL);
	emPanelAddChild(WleGlobalGenesisUI.panel, combo, true);
	ui_ComboBoxSetSelected(combo, zmapGetGenesisEditType(NULL));
	eaPush(&WleGlobalGenesisUI.alwaysActiveGenesisWidgets, UI_WIDGET(combo));
	WleGlobalGenesisUI.edit_mode_combo = combo;
	y = elUINextY(combo) + 5;

	combo = ui_ComboBoxCreateWithEnum(10, y, 235, GenesisViewTypeUIEnum, wleGenesisSelectViewType, NULL);
	emPanelAddChild(WleGlobalGenesisUI.panel, combo, true);
	eaPush(&WleGlobalGenesisUI.genesisWidgets, UI_WIDGET(combo));
	ui_ComboBoxSetSelected(combo, GENESIS_VIEW_FULL);
	y = elUINextY(combo) + 5;

	button = ui_ButtonCreate("Import Map Description", 10, y, wleGenesisUIImportCB, NULL);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(WleGlobalGenesisUI.panel, button, true);
	eaPush(&WleGlobalGenesisUI.genesisWidgets, UI_WIDGET(button));
	y = elUINextY(button) + 5;

	button = ui_ButtonCreate("Export Map Description", 10, y, wleGenesisUIExportCB, NULL);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(WleGlobalGenesisUI.panel, button, true);
	eaPush(&WleGlobalGenesisUI.alwaysActiveGenesisWidgets, UI_WIDGET(button));
	y = elUINextY(button) + 5;

	button = ui_ButtonCreate("Edit Map Description", 10, y, wleGenesisUIEditOrViewCB, NULL);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(WleGlobalGenesisUI.panel, button, true);
	eaPush(&WleGlobalGenesisUI.alwaysActiveGenesisWidgets, UI_WIDGET(button));
	WleGlobalGenesisUI.edit_button = button;
	y = elUINextY(button) + 5;

	button = ui_ButtonCreate("Reseed All", 10, y, wleGenesisUIReseedCB, NULL);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(WleGlobalGenesisUI.panel, button, true);
	eaPush(&WleGlobalGenesisUI.genesisWidgets, UI_WIDGET(button));
	y = elUINextY(button) + 5;

	button = ui_ButtonCreate("Reseed Detail", 10, y, wleGenesisUIReseedDetailCB, NULL);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(WleGlobalGenesisUI.panel, button, true);
	eaPush(&WleGlobalGenesisUI.genesisWidgets, UI_WIDGET(button));
	y = elUINextY(button) + 5;

	button = ui_ButtonCreate("Commit to Layers", 10, y, wleGenesisUICommitCB, NULL);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(WleGlobalGenesisUI.panel, button, true);
	eaPush(&WleGlobalGenesisUI.genesisWidgets, UI_WIDGET(button));
	y = elUINextY(button) + 5;

	if (isProductionEditMode())
	{
		button = ui_ButtonCreate("Preview...", 10, y, wleGenesisUIPreviewCB, NULL);
		ui_WidgetSetWidth(UI_WIDGET(button), 235);
		emPanelAddChild(WleGlobalGenesisUI.panel, button, true);
		eaPush(&WleGlobalGenesisUI.genesisWidgets, UI_WIDGET(button));
		y = elUINextY(button) + 5;
	}
}

static void wleMiscUICollectDoorDestStatusCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetCollectDoorDestStatus(zminfo, *((bool*)userdata));
}

static void wleMiscUICollectDoorDestStatusCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUICollectDoorDestStatusCommitCB, &newState);
}

static void wleMiscUIDisableDuelsCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetDisableDuels(zminfo, *((bool*)userdata));
}

static void wleMiscUIDisableDuelsCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUIDisableDuelsCommitCB, &newState);
}

static void wleMiscUIPowersRequireValidTargetCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetPowersRequireValidTarget(zminfo, *((bool*)userdata));
}

static void wleMiscUIPowersRequireValidTargetCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUIPowersRequireValidTargetCommitCB, &newState);
}

static void wleMiscUIEnableShardVariablesCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetEnableShardVariables(zminfo, *((bool*)userdata));
}

static void wleMiscUIEnableShardVariablesCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUIEnableShardVariablesCommitCB, &newState);
}

static void wleMiscUIDisableInstanceChangingCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetDisableInstanceChanging(zminfo, *((bool*)userdata));
}

static void wleMiscUIDisableInstanceChangingCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUIDisableInstanceChangingCommitCB, &newState);
}

static void wleMiscUITeamNotRequiredCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetTeamNotRequired(zminfo, *((bool*)userdata));
}

static void wleMiscUITeamNotRequiredCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUITeamNotRequiredCommitCB, &newState);
}

static void wleMiscUIGuildOwnedCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetGuildOwned(zminfo, *((bool*)userdata));
}

static void wleMiscUIGuildOwnedCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUIGuildOwnedCommitCB, &newState);
}

static void wleMiscUIGuildNotRequiredCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetGuildNotRequired(zminfo, *((bool*)userdata));
}

static void wleMiscUIGuildNotRequiredCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUIGuildNotRequiredCommitCB, &newState);
}

static void wleMiscUITerrainStaticLightingCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetTerrainStaticLighting(zminfo, *((bool*)userdata));
}

static void wleMiscUITerrainStaticLightingCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState;

	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUITerrainStaticLightingCommitCB, &newState);
	gfxInvalidateAllLightCaches();
}

static void wleMiscUIWindRadiusCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetWindLargeObjectRadiusThreshold(zminfo, *((F32*)userdata));
}

static void wleMiscUIWindRadiusCB(UISpinnerEntry *spinner, UserData userData)
{
	static F32 newRadValue = 0;
	newRadValue = ui_SpinnerEntryGetValue(spinner);
	wleOpLockZoneMap(wleMiscUIWindRadiusCommitCB, &newRadValue);
}

static void wleMiscUIRecordPlayerMatchStatsCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetRecordPlayerMatchStats(zminfo, *((bool*)userdata));
}

static void wleMiscUIRecordPlayerMatchStatsCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUIRecordPlayerMatchStatsCommitCB, &newState);
}

static void wleMiscUIEnableUpsellFeaturesCommitCB(ZoneMapInfo *zminfo, bool success, void *userdata)
{
	if (success) 
		zmapInfoSetEnableUpsellFeatures(zminfo, *((bool*)userdata));
}

static void wleMiscUIEnableUpsellFeaturesCB(UICheckButton *checkbutton, UserData userData)
{
	static bool newState = false;
	newState = ui_CheckButtonGetState(checkbutton);
	wleOpLockZoneMap(wleMiscUIEnableUpsellFeaturesCommitCB, &newState);
}


void wleMiscUICreate(EMEditorDoc *doc)
{
	UISpinnerEntry *spinner;
	UICheckButton *checkbutton;
	F32 y = 5;
	EMPanel* miscPanel;

	//Create Panel and add to the list
	miscPanel = emPanelCreate("Map", "Misc", 0);
	eaPush(&doc->em_panels, miscPanel);

	emPanelAddChild(miscPanel, ui_LabelCreate("Collect Door Destination Status", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetCollectDoorDestStatus(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUICollectDoorDestStatusCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.collectDoorStatusCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Enable Shard Variables", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetEnableShardVariables(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUIEnableShardVariablesCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.shardVariablesCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Disable Duels", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetDisableDuels(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUIDisableDuelsCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.duelsCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Powers Require Valid Target", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetPowersRequireValidTarget(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUIPowersRequireValidTargetCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.powersRequireValidTargetCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Disable Instance Changing", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetDisableInstanceChanging(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUIDisableInstanceChangingCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.disableInstanceChangeCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Allow Unteamed on Owned Map", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetTeamNotRequired(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUITeamNotRequiredCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.unteamedOwnedMapCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Guild Owned Map", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetIsGuildOwned(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUIGuildOwnedCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.guildOwnedCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Allow Non-Guild Members On Guild Owned Map", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetGuildNotRequired(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUIGuildNotRequiredCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.guildNotRequiredCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Calculate Terrain Static Lighting", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetTerrainStaticLighting(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUITerrainStaticLightingCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.terrainStaticLightingCheck = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Wind Large Object Radius Threshold", 0, y), true);
	spinner = ui_SpinnerEntryCreate(0, 500, 1, zmapInfoGetWindLargeObjectRadiusThreshold(NULL), true);
	ui_WidgetSetPosition(UI_WIDGET(spinner), 250, y);
	emPanelAddChild(miscPanel, spinner, true);
	ui_SpinnerEntrySetCallback(spinner, wleMiscUIWindRadiusCB, NULL);
	y = elUINextY(spinner) + 5;

	emPanelAddChild(miscPanel, ui_LabelCreate("Record Player Match Stats", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetRecordPlayerMatchStats(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUIRecordPlayerMatchStatsCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.recordPlayerMatchStats = checkbutton;

	emPanelAddChild(miscPanel, ui_LabelCreate("Enable Upsell Features", 0, y), true);
	checkbutton = ui_CheckButtonCreate(250, y, "", zmapInfoGetEnableUpsellFeatures(zmapGetInfo(worldGetActiveMap())));
	emPanelAddChild(miscPanel, checkbutton, true);
	ui_CheckButtonSetToggledCallback(checkbutton, wleMiscUIEnableUpsellFeaturesCB, NULL);
	y = elUINextY(checkbutton) + 5;
	editorUIState->miscPropertiesUI.enableUpsellFeatures = checkbutton;


}

bool wleIsScratchVisible()
{
	return (editorUIState->showingScratchLayer);
}

UILabel* wleCreateLabel(EMPanel *panel, UIWidget *previous, const char *str)
{
	UILabel *label = ui_LabelCreate(str, 0, previous ? (previous->y + 25) * previous->scale : 0);
	emPanelAddChild(panel, label, false);
	return label;
}

/********************
* MAIN
********************/
/******
* This function creates all of the world editor's widgets.
* PARAMS
*   windowList - all of the created windows will be added to this UIWindow EArray
******/
void wleUIInit(EMEditorDoc *doc)
{
	EMPanel *panel;
	UILabel *label;
	UIButton *button;
	UICheckButton *check;
	UIComboBox *combo;
	UITree *tree;
	UITextEntry *entry;
	UIMessageEntry *messageEntry;
	UIList *list;
	UIRadioButton *radio;
	UIExpressionEntry *exprEntry;
	int leftPad, i, j, nextY;
	bool bMultipleDifficulties = (encounter_GetEncounterDifficultiesCount() > 1);

	char **filterNames = NULL;
	const char *marqueeFilterName = NULL;
	const char *searchFilterName = NULL;
	int *filterVals = NULL;

	resRequestAllResourcesInDictionary(g_hCharacterClassCategorySetDict);

	aiRequestEditingData();

	editorUIState->lockedObjLib = NULL;
	editorUIState->searchFilters = StructCreate(parse_WleFilterList);
	ui_ComboBoxSetModelNoCallback(editorUIState->toolbarUI.marqueeFilterCombo, parse_WleFilter, &editorUIState->searchFilters->filters);

	// create/load skins and fonts
	editorUIState->skinBlue = ui_SkinCreate(NULL);
	editorUIState->skinBlue->entry[0] = colorFromRGBA(0xAAAAFFFF);
	editorUIState->skinGreen = ui_SkinCreate(NULL);
	editorUIState->skinGreen->entry[0] = colorFromRGBA(0xAAFFAAFF);
	editorUIState->skinRed = ui_SkinCreate(NULL);
	editorUIState->skinRed->entry[0] = colorFromRGBA(0xFFAAAAFF);

	// object library
	panel = emPanelCreate("Assets", "Object Library", 800);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, true);
	editorUIState->objectTreeUI.panel = panel;

	check = ui_CheckButtonCreate(0, 0, "Show hidden", false);
	check->statePtr = &editorUIState->showHiddenLibs;
	ui_CheckButtonSetToggledCallback(check, wleUIObjectTreeShowHiddenToggle, NULL);
	emPanelAddChild(panel, check, false);
	check = ui_CheckButtonCreate(0, elUINextY(check), "Replace on create", false);
	check->statePtr = &editState.replaceOnCreate;
	ui_CheckButtonSetToggledCallback(check, wleUIObjectTreeReplaceOnCreateToggle, NULL);
	emPanelAddChild(panel, check, false);
	check = ui_CheckButtonCreate(0, elUINextY(check), "Repeat create across selection", false);
	check->statePtr = &editState.repeatCreateAcrossSelection;
	ui_CheckButtonSetToggledCallback(check, wleUIObjectTreeRepeatCreateToggle, NULL);
	emPanelAddChild(panel, check, false);

	tree = ui_TreeCreate(0, 0, 1, 1);
	tree->selectedF = wleUIObjectNodeSelect;
	tree->activatedF = wleUIObjectNodeActivate;
	ui_TreeSetContextCallback(tree, wleUIObjectNodeRClick, NULL);
	tree->widget.offsetFrom = UITopLeft;
	tree->widget.widthUnit = tree->widget.heightUnit = UIUnitPercentage;
	tree->root.contents = objectLibraryGetRoot();
	tree->root.table = parse_ResourceGroup;
	tree->root.fillF = wleUIObjectNodeFill;
	assert(tree->root.contents);
	ui_TreeNodeExpand(&tree->root);
	emPanelAddChild(panel, tree, false);
	editorUIState->objectTreeUI.objectTree = tree;

	button = ui_ButtonCreate("Collapse all", 0, elUINextY(check), wleUIObjectLibraryCollapseAllClicked, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 1, UIUnitPercentage);
	emPanelAddChild(panel, button, false);
	tree->widget.topPad = elUINextY(button) + 5;

	button = ui_ButtonCreate("Search for object...", 0, 0, wleUIObjectLibrarySearchClicked, NULL);
	button->widget.offsetFrom = UIBottomLeft;
	ui_WidgetSetWidthEx(UI_WIDGET(button), 1, UIUnitPercentage);
	emPanelAddChild(panel, button, false);
	tree->widget.bottomPad = elUINextY(button) + 5;

	wleUITrackerTreeCreate(doc);

	// selection search
	panel = emPanelCreate("Selection", "Search", 0);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, true);
	editorUIState->trackerSearchUI.searchPanel = panel;

	label = wleCreateLabel(panel, NULL, "Filter");
	combo = ui_ComboBoxCreate(0, label->widget.y, 1, parse_WleFilter, &editorUIState->searchFilters->filters, "name");
	editorUIState->trackerSearchUI.searchFilterCombo = combo;
	combo->widget.leftPad = elUINextX(label) + 5;
	combo->widget.widthUnit = UIUnitPercentage;
	ui_ComboBoxSetSelectedCallback(combo, wleUISearchFilterSelected, NULL);
	emPanelAddChild(panel, combo, false);
	button = ui_ButtonCreate("Edit Filters", 0, combo->widget.y, wleUIFiltersEditClicked, combo);
	button->widget.offsetFrom = UITopRight;
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreate("Clear", elUINextX(button), 0, wleUISearchFilterClearClicked, NULL);
	button->widget.offsetFrom = UITopRight;
	emPanelAddChild(panel, button, false);
	combo->widget.rightPad = elUINextX(button);

	label = wleCreateLabel(panel, UI_WIDGET(button), "Find");
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUISearchFilterParamsChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	entry->widget.leftPad = elUINextX(label) + 5;
	emPanelAddChild(panel, entry, false);

	check = ui_CheckButtonCreate(0, elUINextY(entry), "Select on find", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SearchSelect", 0));
	ui_CheckButtonSetToggledCallback(check, wleUISearchSelectCheckToggled, NULL);
	editorUIState->trackerSearchUI.selectCheck = check;
	emPanelAddChild(panel, check, false);
	check = ui_CheckButtonCreate(0, elUINextY(check), "Unique results only", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "SearchUnique", 0));
	ui_CheckButtonSetToggledCallback(check, wleUISearchUniqueCheckToggled, NULL);
	editorUIState->trackerSearchUI.uniqueCheck = check;
	emPanelAddChild(panel, check, false);
	button = ui_ButtonCreate("Previous", 0, elUINextY(check) + 5, wleUISearchFindPrevClicked, entry);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.5f, UIUnitPercentage);
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreate("Next", 0, button->widget.y, wleUISearchFindNextClicked, entry);
	button->widget.xPOffset = 0.5f;
	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.5f, UIUnitPercentage);
	emPanelAddChild(panel, button, true);
	button = ui_ButtonCreate("Select All", 0, elUINextY(button), wleUISearchSelectAllClicked, entry);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 1, UIUnitPercentage);
	emPanelAddChild(panel, button, true);

	wleUIAttribViewPanelCreate(doc);

	// map properties
	panel = emPanelCreate("Map", "Map Properties", 0);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, true);
	editorUIState->mapPropertiesUI.mapPanel = panel;

	label = wleCreateLabel(panel, NULL, "");
	editorUIState->mapPropertiesUI.mapPathLabel = label;
	label = ui_LabelCreate("Public name", 0, elUINextY(label));
	emPanelAddChild(panel, label, false);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesPublicNameChanged, NULL);
	entry->widget.width = 1;
	leftPad = elUINextX(label) + 5;
	entry->widget.widthUnit = UIUnitPercentage;
	editorUIState->mapPropertiesUI.publicNameEntry = entry;
	emPanelAddChild(panel, entry, false);
	
	label = wleCreateLabel(panel, UI_WIDGET(label), "Display name");
	messageEntry = ui_MessageEntryCreate(NULL, 0, label->widget.y, 1);
	ui_MessageEntrySetChangedCallback(messageEntry, wleUIMapPropertiesDisplayNameChanged, NULL);
	ui_MessageEntrySetCanEditKey(messageEntry, false);
	ui_MessageEntrySetCanEditScope(messageEntry, false);
	ui_SetActive(UI_WIDGET(messageEntry), false);
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	messageEntry->widget.widthUnit = UIUnitPercentage;
	editorUIState->mapPropertiesUI.displayNameEntry = messageEntry;
	emPanelAddChild(panel, messageEntry, false);
	
	label = wleCreateLabel(panel, UI_WIDGET(label), "Map Type");
	combo = ui_ComboBoxCreate(0, label->widget.y, 1, NULL, NULL, NULL);
	ui_ComboBoxSetEnum(combo, ZoneMapTypeEnum, wleUIMapPropertiesMapTypeChanged, NULL);
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	combo->widget.widthUnit = UIUnitPercentage;
	editorUIState->mapPropertiesUI.mapTypeCombo = combo;
	emPanelAddChild(panel, combo, false);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Map Level");
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_WidgetSetTooltipString(UI_WIDGET(entry), "The default level that the map will use to execute powers, generate rewards, etc.");
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesLevelChanged, NULL);
	ui_TextEntrySetValidateCallback(entry, ui_TextEntryValidationIntegerOnly, NULL);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	editorUIState->mapPropertiesUI.mapLevelEntry = entry;
	emPanelAddChild(panel, entry, false);

	if(bMultipleDifficulties)
	{
		label = wleCreateLabel(panel, UI_WIDGET(label), "Map Difficulty");
		entry = ui_TextEntryCreateWithEnumCombo("", 0, label->widget.y, EncounterDifficultyEnum, true, true, true, true);
		if(entry->cb)
			entry->cb->bDontSortList = true;
		ui_WidgetSetTooltipString(UI_WIDGET(entry), "The default difficulty that the map will use to spawn encounters.");
		ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesDifficultyChanged, NULL);
		entry->widget.width = 1;
		entry->widget.widthUnit = UIUnitPercentage;
		leftPad = MAX(leftPad, elUINextX(label) + 5);
		editorUIState->mapPropertiesUI.mapDifficultyEntry = entry;
		emPanelAddChild(panel, entry, false);
	}

	label = wleCreateLabel(panel, UI_WIDGET(label), "Default Queue Def");
	entry = ui_TextEntryCreateWithGlobalDictionaryCombo("",0, label->widget.y, "QueueDef", "resourceName", true, true, false, true);
	ui_WidgetSetTooltipString(UI_WIDGET(entry), "Useful for debugging PVP maps. Set this to a default queue def for when your map isn't started by the queue server.");
	ui_TextEntrySetFinishedCallback(entry, welUIMapPropertiesDefaultQueueDefChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.DefaultQueueEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Default Game Type");
	entry = ui_TextEntryCreate("",0,label->widget.y);
	combo = ui_ComboBoxCreate(0, label->widget.y, 1, NULL, NULL, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(combo), "Useful for debugging PVP maps. Set this to a default game type for when you map isn't started by the queue server.");
	ui_ComboBoxSetEnum(combo, PVPGameTypeEnum, wleUIMapPropertiesDefaultPVPGameTypeChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(combo), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.DefaultPVPGameTypeCombo = combo;
	emPanelAddChild(panel, combo, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Force Team Size");
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_WidgetSetTooltipString(UI_WIDGET(entry), "The team size this map should run at.  Zero means to use actual number of players.  Use 1 to 5 to force map to play at that team size regardless of players present");
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesForceTeamSizeChanged, NULL);
	ui_TextEntrySetValidateCallback(entry, ui_TextEntryValidationIntegerOnly, NULL);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	editorUIState->mapPropertiesUI.mapForceTeamSizeEntry = entry;
	emPanelAddChild(panel, entry, true);

	check = ui_CheckButtonCreate(0, elUINextY(label) + 5, "No Team Bonus XP", false);
	emPanelAddChild(panel, check, true);
	ui_CheckButtonSetToggledCallback(check, wleUIMapIgnoreTeamSizeBonusXPChanged, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(check), "If this is checked the team size bonus is not applied for entity kills on this map.");
	editorUIState->mapPropertiesUI.ignoreTeamSizeBonusXPButton = check;
	
	check = ui_CheckButtonCreate(0, elUINextY(check) + 5, "Used In UGC", false);
	emPanelAddChild(panel, check, true);
	ui_CheckButtonSetToggledCallback(check, wleUIMapUsedInUGCChanged, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(check), "If checked, this generates extra data needed to use this map in UGC." );
	editorUIState->mapPropertiesUI.usedInUgcButton = check;

	label = wleCreateLabel(panel, UI_WIDGET(check), "Private to");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	FillAllGroups(&editorUIState->mapPropertiesUI.privateToList);
	FillAllUsers(&editorUIState->mapPropertiesUI.privateToList);
	eaQSort(editorUIState->mapPropertiesUI.privateToList, strCmp);
	entry = ui_TextEntryCreateWithStringMultiCombo("", 0, label->widget.y, &editorUIState->mapPropertiesUI.privateToList, true, true, true, false);
	entry->pchIndexSeparator = ",";
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesPrivacyChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.privateToEntry = entry;
	emPanelAddChild(panel, entry, true);

#if 0
	wleFillTagsList(&ui->ppTagsList);
	ui->pNewTagsEntry = ui_TextEntryCreateWithStringMultiCombo("", 5, y, &ui->ppTagsList, true, true, false, false);
	ui_WidgetSetDimensionsEx(UI_WIDGET(ui->pNewTagsEntry), 1, 20, UIUnitPercentage, UIUnitFixed);
	ui->pNewTagsEntry->pchIndexSeparator = ", ";
	ui_WindowAddChild(ui->pWindow, ui->pNewTagsEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(ui->pNewTagsEntry)) + 5;
#endif

	label = wleCreateLabel(panel, UI_WIDGET(label), "Parent Map");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreateWithGlobalDictionaryCombo("", 0, label->widget.y, g_ZoneMapDictionary, "resourceName", true, true, false, true);
	ui_TextEntrySetFinishedCallback(entry, wleUIParentMapNameChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.parentMapEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Parent Map Spawn");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUIParentMapSpawnChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.parentMapSpawnEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Start Spawn Point");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUIStartSpawnNameChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.startSpawnEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Respawn Type");
	combo = ui_ComboBoxCreate(0,label->widget.y, 1, NULL, NULL, NULL);
	ui_ComboBoxSetEnum(combo,ZoneRespawnTypeEnum,wleUIMapPropertiesRespawnTypeChanged,NULL);
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	combo->widget.widthUnit = UIUnitPercentage;
	editorUIState->mapPropertiesUI.respawnTypeCombo = combo;
	emPanelAddChild(panel, combo, false);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Respawn Wave Time");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreate("0", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesRespawnWaveTimeChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.respawnWaveTimeEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Respawn Min Time");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreate("0", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesRespawnMinTimeChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.respawnMinTimeEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Respawn Max Time");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreate("0", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesRespawnMaxTimeChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.respawnMaxTimeEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Respawn Increment Time");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreate("0", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesRespawnIncrTimeChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.respawnIncrTimeEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Respawn Attrition Time");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreate("0", 0, label->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleUIMapPropertiesRespawnAttrTimeChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.respawnAttrTimeEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Reward Table");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	combo = ui_ComboBoxCreateWithGlobalDictionary(0, label->widget.y, 1, "RewardTable", "resourceName");
	ui_ComboBoxSetSelectedCallback(combo, wleUIRewardTableChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(combo), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.rewardTableCombo = combo;
	emPanelAddChild(panel, combo, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Player Reward Table");
	ui_WidgetSetTooltipString(UI_WIDGET(label), "Reward table to be granted when a player dies.");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = entry = ui_TextEntryCreateWithGlobalDictionaryCombo("",0, label->widget.y, "RewardTable", "resourceName", true, true, false, true);
	ui_TextEntrySetFinishedCallback(entry, wleUIPlayerRewardTableChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.playerRewardTableEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Requires Expr");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	exprEntry = ui_ExpressionEntryCreate("", zmapGetExprContext());
	ui_ExpressionEntrySetChangedCallback(exprEntry, wleUIRequiresExprChanged, NULL);
	ui_WidgetSetPosition(UI_WIDGET(exprEntry), 0, label->widget.y);
	ui_WidgetSetWidthEx(UI_WIDGET(exprEntry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.requiresExprEntry = exprEntry;
	emPanelAddChild(panel, exprEntry, true);
	
	label = wleCreateLabel(panel, UI_WIDGET(label), "Permission Expr");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	exprEntry = ui_ExpressionEntryCreate("", zmapGetExprContext());
	ui_ExpressionEntrySetChangedCallback(exprEntry, wleUIPermissionExprChanged, NULL);
	ui_WidgetSetPosition(UI_WIDGET(exprEntry), 0, label->widget.y);
	ui_WidgetSetWidthEx(UI_WIDGET(exprEntry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.permissionExprEntry = exprEntry;
	emPanelAddChild(panel, exprEntry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Required Class Category Set");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreateWithGlobalDictionaryCombo("", 0, label->widget.y, g_hCharacterClassCategorySetDict, "resourceName", true, true, true, true);
	ui_TextEntrySetFinishedCallback(entry, wleUIRequiredClassCategorySetComboChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.requiredClassCategorySetEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Mastermind Def");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreateWithGlobalDictionaryCombo("", 0, label->widget.y, g_pcAIMasterMindDefDictName, "resourceName", true, true, true, true);
	ui_TextEntrySetFinishedCallback(entry, wleUIMastermindDefChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.mastermindDefEntry = entry;
	emPanelAddChild(panel, entry, true);

	label = wleCreateLabel(panel, UI_WIDGET(label), "Civilian Map Def");
	leftPad = MAX(leftPad, elUINextX(label) + 5);
	entry = ui_TextEntryCreateWithGlobalDictionaryCombo("", 0, label->widget.y, g_pcAICivDefDictName, "resourceName", true, true, true, true);
	ui_TextEntrySetFinishedCallback(entry, wleUICivilianMapDefChanged, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	editorUIState->mapPropertiesUI.civilianMapDefEntry = entry;
	emPanelAddChild(panel, entry, false);

	check = ui_CheckButtonCreate(0, elUINextY(label) + 5, "Disable visited tracking", false);
	emPanelAddChild(panel, check, true);
	ui_CheckButtonSetToggledCallback(check, wleUIVisitedTrackerToggled, NULL);
	editorUIState->mapPropertiesUI.disableVisitedTrackingButton = check;

	editorUIState->mapPropertiesUI.publicNameEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.displayNameEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.mapTypeCombo->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.DefaultQueueEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.DefaultPVPGameTypeCombo->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.mapLevelEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.mapForceTeamSizeEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.privateToEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.parentMapEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.parentMapSpawnEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.startSpawnEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.rewardTableCombo->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.playerRewardTableEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.requiresExprEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.permissionExprEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.requiredClassCategorySetEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.mastermindDefEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.civilianMapDefEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.respawnTypeCombo->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.respawnWaveTimeEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.respawnMinTimeEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.respawnMaxTimeEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.respawnIncrTimeEntry->widget.leftPad = leftPad;
	editorUIState->mapPropertiesUI.respawnAttrTimeEntry->widget.leftPad = leftPad;
	if(editorUIState->mapPropertiesUI.mapDifficultyEntry) {
		editorUIState->mapPropertiesUI.mapDifficultyEntry->widget.leftPad = leftPad;
	}

	wleUIMapPropertiesRefresh();

	// variable properties
	panel = emPanelCreate("Map", "Map Variables", 0);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, false);
	editorUIState->variablePropertiesUI.variablePanel = panel;

	wleUIVariablePropertiesRefresh();
 
	// Global GAE Layers
	sndCommonAddChangedCallback(wleUIReloadGlobalGAELayersCB, NULL);
	wleUISetupGlobalGAELayers();

	panel = emPanelCreate("Map", "Global GAE Layers", 0);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, false);
	editorUIState->globalGAELayersUI.globalGAELayersPanel = panel;

	wleUIGlobalGAELayersRefresh();

	// map layers
	wleUIMapLayersRefresh();

	// region manager
	wleUIRegionMngrSettingsInit();
	panel = emPanelCreate("Map", "Regions", 500);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, true);
	editorUIState->regionMngrUI.panel = panel;
	tree = ui_TreeCreate(0, 0, 1, 150);
	editorUIState->regionMngrUI.tree = tree;

	button = ui_ButtonCreate("Show All", 0, 0, wleUIRegionMngrTintAllClicked, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.leftPad = 0;
	button->widget.rightPad = 3;
	emPanelAddChild(panel, button, false);
	button = ui_ButtonCreate("Hide All", 0, 0, wleUIRegionMngrUntintAllClicked, NULL);
	button->widget.xPOffset = 0.5;
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.leftPad = 3;
	button->widget.rightPad = 0;
	emPanelAddChild(panel, button, false);
	tree->widget.widthUnit = UIUnitPercentage;
	tree->widget.y = elUINextY(button) + 5;
	ui_TreeNodeSetFillCallback(&tree->root, wleUIRegionMngrRootFill, NULL);
	ui_TreeNodeExpand(&tree->root);
	ui_TreeSetContextCallback(tree, wleUIRegionMngrTreeRClick, NULL);
	ui_TreeSetActivatedCallback(tree, wleUIRegionMngrTreeActivated, NULL);
	ui_TreeSetDragCallback(tree, wleUIRegionMngrTreeDrag, NULL);
	ui_TreeEnableDragAndDrop(tree);
	emPanelAddChild(panel, tree, true);

	label = ui_LabelCreate("Region Properties:", 0, elUINextY(tree) + 5);
	emPanelAddChild(panel, label, false);

	label = ui_LabelCreate("Override Cubemap", 5, elUINextY(label) + 5);
	emPanelAddChild(panel, label, false);
	button = ui_ButtonCreate("", elUINextX(label) + 5, label->widget.y, wleUIRegionMngrOverrideCubemapClicked, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 1, UIUnitPercentage);
	ui_SetActive(UI_WIDGET(button), false);
	emPanelAddChild(panel, button, false);
	editorUIState->regionMngrUI.cubemapOverrideButton = button;

	label = ui_LabelCreate("Max Pets", 5, elUINextY(button) + 5);
	emPanelAddChild(panel, label, false);
	entry = ui_TextEntryCreate("", elUINextX(label) + 5, label->widget.y);
	editorUIState->regionMngrUI.maxPetsEntry = entry;
	ui_TextEntrySetIntegerOnly(entry);
	ui_TextEntrySetFinishedCallback(entry, wleUIRegionMngrMaxPetsChanged, NULL);
	emPanelAddChild(panel, entry, false);

	label = ui_LabelCreate("Vehicle Rules", 5, elUINextY(entry) + 5);
	emPanelAddChild(panel, label, false);
	entry = ui_TextEntryCreateWithEnumCombo("", elUINextX(label), label->widget.y,VehicleRulesEnum,true,true,true,true);
	editorUIState->regionMngrUI.vehicleRulesEntry = entry;
	ui_TextEntrySetFinishedCallback(entry, wleUIRegionMngrVehicleRulesChanged,NULL);
	emPanelAddChild(panel, entry, false);

	check = ui_CheckButtonCreate(5, elUINextY(entry) + 5, "Cluster World Geometry", 0);
	ui_CheckButtonSetToggledCallback(check, wleUIRegionMngrClusterWorldGeoChanged, NULL);
	emPanelAddChild(panel, check, false);
	editorUIState->regionMngrUI.clusterWorldGeoButton = check;

	check = ui_CheckButtonCreate(5, elUINextY(check), "Use Indoor Lighting Mode", 0);
	ui_CheckButtonSetToggledCallback(check, wleUIRegionMngrIndoorLightingChanged, NULL);
	emPanelAddChild(panel, check, false);
	editorUIState->regionMngrUI.indoorLightingButton = check;

	label = ui_LabelCreate("Region Type:", 0, elUINextY(check) + 5);
	emPanelAddChild(panel, label, false);
	editorUIState->regionMngrUI.typeRadioGroup = ui_RadioButtonGroupCreate();
	nextY = elUINextY(label);
	for (i = 0; i < WRT_COUNT; ++i)
	{
		radio = ui_RadioButtonCreate(5, nextY, StaticDefineIntRevLookup(WorldRegionTypeEnum, i), editorUIState->regionMngrUI.typeRadioGroup);
		ui_RadioButtonSetToggledCallback(radio, wleUIRegionMngrTypeToggled, NULL);
		emPanelAddChild(panel, radio, false);
		nextY = elUINextY(radio);
	}

	label = ui_LabelCreate("Skies:", 0, nextY + 5);
	emPanelAddChild(panel, label, false);
	list = ui_ListCreate(NULL, NULL, 13);
	editorUIState->regionMngrUI.sky_list = list;
	ui_ListAppendColumn(list, ui_ListColumnCreateText("", elUIListTextDisplay, NULL));
	ui_ListSetActivatedCallback(list, wleUIRegionMngrSkyActivated, NULL);
	list->widget.y = elUINextY(label);
	list->widget.widthUnit = UIUnitPercentage;
	list->widget.width = 1;
	list->widget.height = 150;
	list->widget.leftPad = list->widget.rightPad = 5;
	emPanelAddChild(panel, list, false);
	ui_TreeSetSelectedCallback(tree, wleUIRegionMngrTreeSelected, list);
	{
		F32 y = elUINextY(list) + 5;
		button = ui_ButtonCreate("Up", 0, y, wleUIRegionMngrMoveSkyUp, list);
		button->widget.width = 0.5;
		button->widget.widthUnit = UIUnitPercentage;
		button->widget.leftPad = 5;
		button->widget.rightPad = 3;
		emPanelAddChild(panel, button, false);
		button = ui_ButtonCreate("Down", 0, y, wleUIRegionMngrMoveSkyDown, list);
		button->widget.xPOffset = 0.5;
		button->widget.width = 0.5;
		button->widget.widthUnit = UIUnitPercentage;
		button->widget.leftPad = 3;
		button->widget.rightPad = 5;
		emPanelAddChild(panel, button, false);
		y = elUINextY(button) + 5;
		button = ui_ButtonCreate("Add", 0, y, wleUIRegionMngrAddSky, list);
		button->widget.width = 0.5;
		button->widget.widthUnit = UIUnitPercentage;
		button->widget.leftPad = 5;
		button->widget.rightPad = 3;
		emPanelAddChild(panel, button, false);
		button = ui_ButtonCreate("Remove", 0, y, wleUIRegionMngrRemoveSky, list);
		button->widget.xPOffset = 0.5;
		button->widget.width = 0.5;
		button->widget.widthUnit = UIUnitPercentage;
		button->widget.leftPad = 3;
		button->widget.rightPad = 5;
		emPanelAddChild(panel, button, true);
	}

	// lock manager
	panel = emPanelCreate("Files", "Lock Manager", 300);
	eaPush(&doc->em_panels, panel);
	emPanelSetOpened(panel, true);
	editorUIState->lockMngrUI.panel = panel;
	tree = ui_TreeCreate(0, 0, 1, 1);

	editorUIState->lockMngrUI.tree = tree;
	tree->widget.widthUnit = tree->widget.heightUnit = UIUnitPercentage;
	ui_TreeSetActivatedCallback(tree, wleUILockMngrTreeActivated, NULL);
	ui_TreeSetContextCallback(tree, wleUILockMngrTreeRClick, NULL);
	emPanelAddChild(panel, tree, false);
	button = ui_ButtonCreate("Add/Load Object Library File...", 0, 0, wleUILockMngrAdd, NULL);
	button->widget.offsetFrom = UIBottomLeft;
	button->widget.width = 1;
	button->widget.widthUnit = UIUnitPercentage;
	emPanelAddChild(panel, button, false);
	tree->widget.bottomPad = elUINextY(button) + 5;
	ui_TreeNodeSetFillCallback(&tree->root, wleUILockMngrFillRoot, NULL);
	ui_TreeNodeExpand(&tree->root);

	// attribute editor
	wleAECreate(doc);

	// bookmarks and notes
	wleNotesCreate(doc);

	// genesis
	wleGenesisUICreate(doc);
	
	//misc stuff
	wleMiscUICreate(doc);

	// auto-lock prompt
	wleUILockPromptCreate();

	//Event Debug Window
	wleEventDebugWindowCreate(doc);

	// load filter settings
	EditorPrefGetStruct(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SearchFilters", parse_WleFilterList, editorUIState->searchFilters);
	for (i = 0; i < eaSize(&editorUIState->searchFilters->filters); i++)
	{
		if (!editorUIState->searchFilters->filters[i]->name)
			editorUIState->searchFilters->filters[i]->name = StructAllocString("");
		for (j = 0; j < eaSize(&editorUIState->searchFilters->filters[i]->criteria); j++)
		{
			WleFilterCriterion *criterion = editorUIState->searchFilters->filters[i]->criteria[j];
			criterion->criterion = wleCriterionGet(criterion->propertyName);
		}
	}
	marqueeFilterName = EditorPrefGetString(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "MarqueeFilter", "");
	searchFilterName = EditorPrefGetString(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SearchFilter", "");
	for (i = 0; i < eaSize(&editorUIState->searchFilters->filters); i++)
	{
		if (strcmpi(editorUIState->searchFilters->filters[i]->name, marqueeFilterName) == 0)
			ui_ComboBoxSetSelected(editorUIState->toolbarUI.marqueeFilterCombo, i);
		if (strcmpi(editorUIState->searchFilters->filters[i]->name, searchFilterName) == 0)
			ui_ComboBoxSetSelected(editorUIState->trackerSearchUI.searchFilterCombo, i);
	}
}

void wleUIDrawCompass(void)
{
	static AtlasTex *compass = NULL;
	F32 camera_angle;
	Mat4 cam_mat;

	if(!compass)
		compass = atlasLoadTexture("eui_compass_BG");
	gfxGetActiveCameraMatrix(cam_mat);
	if(cam_mat[2][2] != 0.0f)
		camera_angle = -atan(cam_mat[2][0]/cam_mat[2][2]) + (cam_mat[2][2] < 0.0f ? 3.14159f : 0.0f);
	else
		camera_angle = (cam_mat[2][0] > 0.0f ? 0.0f : 3.14159f);
	display_sprite_rotated(compass, g_ui_State.viewportMax[0]-100, g_ui_State.viewportMax[1]-100, camera_angle+3.14159, -2, 1.0f, 0xFFFFFFFF);
}

/******
* This does all relevant world editor UI drawing once per frame.
******/
void wleUIDraw(void)
{
	editorUIState->lockMsgShown = false;

	wleUIRegionMngrDraw();

	// draw special cursors
	if (inpLevelPeek(INP_CONTROL) && !inpLevelPeek(INP_ALT))
		ui_SetCursor("eui_pointer_arrow_plus", NULL, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF);
	else if (inpLevelPeek(INP_ALT) && !inpLevelPeek(INP_CONTROL))
		ui_SetCursor("eui_pointer_arrow_minus", NULL, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF);
}

#endif
AUTO_RUN;
void wleInitWorldVariableDefaultValueTypeSansVariablesEnum( void )
{
#ifndef NO_EDITORS
	int count = 0;
	{
		int it;
		for( it = 0; WorldVariableDefaultValueTypeEnum[it].key != DM_END; ++it )
		{
			if( WorldVariableDefaultValueTypeEnum[it].value == WVARDEF_MAP_VARIABLE ||
				WorldVariableDefaultValueTypeEnum[it].value == WVARDEF_MISSION_VARIABLE) {
				continue;
			}

			++count;
		}
		++count;
	}

	WorldVariableDefaultValueTypeSansVariablesEnum = malloc( sizeof( StaticDefineInt ) * count );
	{
		int srcIt = 0;
		int destIt = 0;
		while( destIt != count ) {
			if( WorldVariableDefaultValueTypeEnum[srcIt].value == WVARDEF_MAP_VARIABLE ||
				WorldVariableDefaultValueTypeEnum[srcIt].value == WVARDEF_MISSION_VARIABLE) {
				++srcIt;
				continue;
			}

			WorldVariableDefaultValueTypeSansVariablesEnum[destIt++] = WorldVariableDefaultValueTypeEnum[srcIt++];
		}
	}
#endif
}

#include "WorldEditorUI_h_ast.c"
