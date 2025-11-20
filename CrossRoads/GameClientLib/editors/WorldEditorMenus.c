#ifndef NO_EDITORS

#include "WorldEditorMenus.h"
#include "WorldEditorPrivate.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorUI.h"
#include "WorldEditorOptions.h"
#include "WorldEditorOperations.h"
#include "WorldGrid.h"
#include "EditorObjectMenus.h"
#include "EditorManager.h"
#include "EditorManagerUtils.h"
#include "EditorPrefs.h"
#include "TerrainEditor.h"
#include "EditLibGizmos.h"
#include "Materials.h"
#include "groupdbmodify.h"
#include "wlState.h"
#include "StringUtil.h"
#include "WorldEditorAppearanceAttributes.h"
#include "EditorPreviewWindow.h"
#include "Materials.h"
#include "../GraphicsLib/GfxTextures.h"

#include "EditorObjectMenus_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct BasicTexture BasicTexture;

/******
* FORWARD DECLARATIONS
******/
void snapNormal(UIMenuItem *menuItem, UserData stuff);
void snapClamp(UIMenuItem *menuItem, UserData stuff);
void menuTranslateSnap(UIAnyWidget *widget, UserData mode);
void showVersionInfo(UIMenuItem *menuItem, UserData *stuff);

/******
* GLOBALS
******/
EdObjCustomMenu *wleMenuRightClick;

/********************
* MENU CALLBACKS
********************/
static void wleUIEditTranslateSnap(UIAnyWidget *widget, UserData mode)
{
	wleSetTranslateSnapMode((EditSpecialSnapMode) mode);
}

static void wleUIEditTranslateSnapNormal(UIMenuItem *menuItem, UserData unused)
{
	wleCmdSnapNormal();
}

static void wleUIEditTranslateSnapClamping(UIMenuItem *menuItem, UserData unused)
{
	wleCmdSnapClamp();
}

static void wleUIViewDisableVolColl(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "DisableVolColl", editorUIState->disableVolColl);
}

static void wleUIViewHideAllVols(UIMenuItem *menuItem, UserData unused)
{
	UIMenuItem *volCollItem = emMenuItemGet(&worldEditor, "disablevolcoll");
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideAllVols", *menuItem->data.statePtr);
	if (volCollItem)
	{
		*volCollItem->data.statePtr = *menuItem->data.statePtr;
		wleUIViewDisableVolColl(volCollItem, NULL);
	}
}

static void wleUIViewHideOccVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideOccVols", *menuItem->data.statePtr);
}

static void wleUIViewHideAudioVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideAudioVols", *menuItem->data.statePtr);
}

static void wleUIViewHideSkyVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideSkyVols", *menuItem->data.statePtr);
}

static void wleUIViewHideNeighborhoodVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideNeighborhoodVols", *menuItem->data.statePtr);
}

static void wleUIViewHideInteractionVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideInteractionVols", *menuItem->data.statePtr);
}

static void wleUIViewHideLandmarkVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideLandmarkVols", *menuItem->data.statePtr);
}

static void wleUIViewHidePowerVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePowerVols", *menuItem->data.statePtr);
}

static void wleUIViewHideWarpVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideWarpVols", *menuItem->data.statePtr);
}

static void wleUIViewHideGenesisVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideGenesisVols", *menuItem->data.statePtr);
}

static void wleUIViewHideExclusionVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideExclusionVols", *menuItem->data.statePtr);
}

static void wleUIViewHideAggroVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideAggroVols", *menuItem->data.statePtr);
}

static void wleUIViewHideUntypedVols(UIMenuItem *menuItem, UserData unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideUntypedVols", *menuItem->data.statePtr);
}

static void wleUIViewHidePatrolPoints(UIMenuItem *menuItem, UserData unused)
{
	bool state = ui_MenuItemGetCheckState(menuItem);

	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePatrolPoints", state);
	if (state)
		edObjSelectionClear(EDTYPE_PATROL_POINT);
	wl_state.debug.hide_encounter_2_patrols = state;
	zmapTrackerUpdate(NULL, true, false);
	wleOpRefreshUI();
}

static void wleUIViewHideEncounterActors(UIMenuItem *menuItem, UserData unused)
{
	bool state = ui_MenuItemGetCheckState(menuItem);

	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideEncounterActors", state);
	if (state)
		edObjSelectionClear(EDTYPE_ENCOUNTER_ACTOR);
	wl_state.debug.hide_encounter_2_actors = state;
	zmapTrackerUpdate(NULL, true, false);
	wleOpRefreshUI();
}

/******
* EDITOR OBJECT CUSTOM MENU INTEGRATION
******/
static bool wleMenuDisableCheck(UserData unused)
{
	return false;
}

/******
* This function initializes the main menu.
******/
void wleMenuInitMenus(void)
{
	GfxVisualizationSettings *visSettings = gfxGetVisSettings();
	UIMenu *submenu;
	EMMenuItemDef wleMenuItems[] =
	{
		{"em_newcurrented", "New ZoneMap...", wleCheckCmdNewZoneMap, NULL, "Editor.NewGrid"},
		{"em_opencurrented", "Open ZoneMap...", wleCheckCmdOpenZoneMap, NULL, "Editor.OpenGrid"},
		{"em_close", "Close", wleMenuDisableCheck},
		{"exportpositions", "Export Positions", NULL, NULL, "wleCmdExpPositionDummies"},
		{"exportgeometry", "Export Geometry", NULL, NULL, "wleExportSelectedToVrml"},
		{"em_cut", "Cut", wleCheckCmdCutCopyPaste, NULL, "EM.Cut"},
		{"em_copy", "Copy", wleCheckCmdCutCopyPaste, NULL, "EM.Copy"},
		{"em_paste", "Paste", wleCheckCmdCutCopyPaste, NULL, "EM.Paste"},
		{"focuscamera", "Focus camera", NULL, NULL, "Editor.FocusCamera"},
		{"uptree", "Select parent", wleCheckCmdUpTree, NULL, "Editor.UpTree"},
		{"downtree", "Select child", wleCheckCmdDownTree, NULL, "Editor.DownTree"},
		{"deselect", "Deselect", wleCheckCmdDeselect, NULL, "Editor.Deselect"},
		{"invselect", "Invert", wleCheckCmdInvertSelection, NULL, "Editor.Invert"},
		{"lockselection", "Toggle selection lock", wleCheckCmdLockSelection, NULL, "Editor.Lock"},
		{"hide", "Hide/Unhide", wleCheckCmdHideSelection, NULL, "Editor.Hide"},
		{"unhide", "Unhide all", wleCheckCmdUnhideAll, NULL, "Editor.Unhide"},
		{"hidevols", "Hide all volumes"},
		{"hideoccvols", "Hide occlusion volumes", wleUIVolumeTypeHideCheck},
		{"hideaudiovols", "Hide audio volumes", wleUIVolumeTypeHideCheck},
		{"hideskyvols", "Hide skyfade volumes", wleUIVolumeTypeHideCheck},
		{"hidehoodvols", "Hide neighborhood volumes", wleUIVolumeTypeHideCheck},
		{"hideinteractionvols", "Hide optional action volumes", wleUIVolumeTypeHideCheck},
		{"hidelandmarkvols", "Hide landmark volumes", wleUIVolumeTypeHideCheck},
		{"hidepowervols", "Hide power volumes", wleUIVolumeTypeHideCheck},
		{"hidewarpvols", "Hide warp volumes", wleUIVolumeTypeHideCheck},
		{"hidegenesisvols", "Hide genesis volumes", wleUIVolumeTypeHideCheck},
		{"hideexclusionvols", "Hide exclusion volumes", wleUIVolumeTypeHideCheck},
		{"hideuntypedvols", "Hide volumes with no type", wleUIVolumeTypeHideCheck},
		{"hideaggrovols", "Hide aggro volumes", wleUIVolumeTypeHideCheck},
		{"disablevolcoll", "Disable volume collision"},
		{"hidevolsubmenu", "Hide volumes"},
		{"hidepatrolpoints", "Hide patrol points"},
		{"hideencounteractors", "Hide encounter actors"},

		{"placeobjsubmenu", "Place"},

		{"placeencobjsubmenu", "Encounters"},
		{"placeencounter", "Place Encounter", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Encounter\""},
		{"placenamedpoint", "Place Named Point", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Named Point\""},
		{"placepatrolroute", "Place Patrol Route", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Patrol Route\""},
		{"placespawnpoint", "Place Spawn Point", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Respawn Spawn Point\""},
		{"placetrigger", "Place Trigger Condition", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Trigger Condition\""},
		{"placepathnode", "Place Path Node", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"PathNode\""},

		{"placesystemobjsubmenu", "System"},
		{"placesound", "Place Sound", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Sound\""},
		{"placevolume", "Place Volume", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Box Volume\""},
		{"placewind", "Place Wind Source", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"WindSource\""},

		{"placeworldobjsubmenu", "World"},
		{"placebuilding", "Place Building Gen", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Buildinggen\""},
		{"placecurve", "Place Curve", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"New Curve\""},
		{"placedebris", "Place Debris Field", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"DebrisField\""},
		{"placefxnode", "Place FX Node", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Node_1ft\""},
		{"placeomnilight", "Place Omni Light", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Omni_Light\""},
		{"placeplanet", "Place Planet Gen", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Planetgen\""},
		{"placespotlight", "Place Spot Light", wleCheckCmdPlaceObject, NULL, "Editor.PlaceObject \"Spot_Light\""},

		{"freeze", "Freeze", wleCheckCmdFreezeSelection, NULL, "Editor.Freeze"},
		{"unfreeze", "Unfreeze all", wleCheckCmdUnfreeze, NULL, "Editor.Unfreeze"},
		{"cyclewidget", "Cycle widget", wleCheckCmdCycleGizmo, NULL, "Editor.TransRot"},
		{"snapnormal", "Snap to normal", wleCheckCmdSnapNormal},
		{"snapclamping", "Snap clamping", wleCheckCmdSnapClamp},
		{"cycletranssnap", "Cycle translate snap", wleCheckCmdCycleTransSnap, NULL, "Editor.CycleTransSnap"},
		{"snapgrid", "Snap to grid", wleCheckCmdSnapNormal},
		{"snapvertex", "Snap to vertex", wleCheckCmdSnapNormal},
		{"snapmidpoint", "Snap to midpoint", wleCheckCmdSnapNormal},
		{"snapedge", "Snap to edge", wleCheckCmdSnapNormal},
		{"snapface", "Snap to face", wleCheckCmdSnapNormal},
		{"snapterrain", "Snap to terrain", wleCheckCmdSnapNormal},
		{"snapsmart", "Auto snap", wleCheckCmdSnapNormal},
		{"snapnone", "No snap", wleCheckCmdSnapNormal},
		{"snapsubmenu", "Snap to", wleCheckCmdSnapNormal},
		{"dectranssnap", "Decrease translation snap", NULL, NULL, "Editor.DecTransSnap"},
		{"inctranssnap", "Increase translation snap", NULL, NULL, "Editor.IncTransSnap"},
		{"cycletransaxes", "Cycle translation axes", wleCheckCmdCycleTransAxes, NULL, "Editor.CycleTransAxes"},
		{"toggletransx", "Toggle x-axis translation", wleCheckCmdToggleTransX, NULL, "Editor.ToggleTransX"},
		{"toggletransy", "Toggle y-axis translation", wleCheckCmdToggleTransY, NULL, "Editor.ToggleTransY"},
		{"toggletransz", "Toggle z-axis translation", wleCheckCmdToggleTransZ, NULL, "Editor.ToggleTransZ"},
		{"togglerotsnap", "Toggle rotation snap", wleCheckCmdToggleRotSnap, NULL, "Editor.ToggleRotSnap"},
		{"decrotsnap", "Decrease rotation snap", NULL, NULL, "Editor.DecRotSnap"},
		{"incrotsnap", "Increase rotation snap", NULL, NULL, "Editor.IncRotSnap"},
		{"worldpivot", "World pivot", wleCheckCmdWorldPivot, NULL, "Editor.WorldPivot"},
		{"resetrot", "Reset rotation", wleCheckCmdResetRot, NULL, "Editor.ResetRot"},
		{"edittags", "Edit Tags", NULL, NULL, "Editor.EditTags"},
		{"lockfiles", "Lock object library files", wleCheckCmdLockFiles, NULL, "Editor.LockFiles"},
		{"findandreplace", "Find and replace...", wleCheckCmdFindAndReplace, NULL, "Editor.FindAndReplace"},
		{"instance", "Instance", wleCheckCmdInstantiate, NULL, "Editor.Instance"},
		{"duplicate", "Duplicate", wleCheckCmdDuplicate, NULL, "Editor.SelectDo duplicate"},
		{"reseed", "Reseed", wleCheckDefault, NULL, "Editor.ReseedAll"},
		{"group", "Group", wleCheckCmdGroup, NULL, "Editor.Group"},
		{"setdefaultparent", "Set default parent", wleCheckCmdSetDefaultParent, NULL, "Editor.SetDefaultParent"},
		{"movetodefaultparent", "Move to default parent", wleCheckCmdMoveToDefaultParent, NULL, "Editor.MoveToDefaultParent"},
		{"groupexpandall", "Expand all", wleCheckCmdGroupExpandAll, NULL, "Editor.GroupExpandAll"},
		{"rename", "Rename", wleCheckCmdRename, NULL, "Editor.Rename"},
		{"select_children", "Select children", wleCheckDefault, NULL, "Editor.SelectChildren"},
		{"addtogroup", "Add to group...", wleCheckCmdAddToGroup, NULL, "Editor.AddToGroup"},
		{"ungroup", "Ungroup", wleCheckCmdUngroup, NULL, "Editor.Ungroup"},
		{"graphicsoptions", "Graphics Options", NULL, NULL, "gfxDebugUI"},
		{"delete", "Delete", wleCheckCmdDelete, NULL, "Editor.SelectDo delete"},
		{"editinstances", "Edit instances", wleCheckCmdEditOrig, NULL, "Editor.EditOrig"},
		{"newlayer", "New layer...", wleCheckCmdNewLayer, NULL, "Editor.NewLayer"},
		{"importlayer", "Import layer...", wleCheckCmdImportLayer, NULL, "Editor.ImportLayer"},
		{"reloadfromsource", "Reload from source", wleCheckCmdReloadFromSource, NULL, "Editor.ReloadFromSource"},
		{"savetolib", "Save to library...", wleCheckCmdSaveToLib, NULL, "Editor.SaveToLib"},
		{"edit_subobjects", "Edit Subobjects", NULL, NULL, "Editor.SelectDo edit_subobjects"},
		{"copynames", "Copy selection names", NULL, NULL, "Editor.CopySelectionNames"},
		{"copytoscratch", "Copy to Scratch Layer", wleCheckCmdCopyToScratch, NULL, "Editor.CopyToScratch"},

		// keep these?
		//	{"locklayer", "Edit layer", NULL, NULL, "Editor.SelectDo lock_layer"},
		//	{"revertlayer", "Close layer", NULL, NULL, "Edit.SelectDo revert_layer"},
		//	{"savelayer", "Save layer", NULL, NULL, "Editor.SelectDo save_layer"},
		//	{"saveandcloselayer", "Save and close layer", NULL, NULL, "Editor.SelectDo save_and_close_layer"},
	};

	PERFINFO_AUTO_START_FUNC();

	// create items
	emMenuItemCreateFromTable(&worldEditor, wleMenuItems, ARRAY_SIZE(wleMenuItems));

	// customize non-command menu items
	emMenuItemSet(&worldEditor, "snapgrid", ui_MenuItemCreate("Snap to grid", UIMenuCheckButton, wleUIEditTranslateSnap, (void*) EditSnapGrid, (void*) false));
	emMenuItemSet(&worldEditor, "snapvertex", ui_MenuItemCreate("Snap to vertex", UIMenuCheckButton, wleUIEditTranslateSnap, (void*) EditSnapVertex, (void*) false));
	emMenuItemSet(&worldEditor, "snapmidpoint", ui_MenuItemCreate("Snap to midpoint", UIMenuCheckButton, wleUIEditTranslateSnap, (void*) EditSnapMidpoint, (void*) false));
	emMenuItemSet(&worldEditor, "snapedge", ui_MenuItemCreate("Snap to edge", UIMenuCheckButton, wleUIEditTranslateSnap, (void*) EditSnapEdge, (void*) false));
	emMenuItemSet(&worldEditor, "snapface", ui_MenuItemCreate("Snap to face", UIMenuCheckButton, wleUIEditTranslateSnap, (void*) EditSnapFace, (void*) false));
	emMenuItemSet(&worldEditor, "snapterrain", ui_MenuItemCreate("Snap to terrain", UIMenuCheckButton, wleUIEditTranslateSnap, (void*) EditSnapTerrain, (void*) false));
	emMenuItemSet(&worldEditor, "snapsmart", ui_MenuItemCreate("Auto snap", UIMenuCheckButton, wleUIEditTranslateSnap, (void*) EditSnapSmart, (void*) false));
	emMenuItemSet(&worldEditor, "snapnone", ui_MenuItemCreate("No snap", UIMenuCheckButton, wleUIEditTranslateSnap, (void*) EditSnapNone, (void*) false));
	emMenuItemSet(&worldEditor, "snapnormal", ui_MenuItemCreate("Snap to normal", UIMenuCheckButton, wleUIEditTranslateSnapNormal, NULL, (void*) false));
	emMenuItemSet(&worldEditor, "snapclamping", ui_MenuItemCreate("Snap clamping", UIMenuCheckButton, wleUIEditTranslateSnapClamping, NULL, (void*) false));
	emMenuItemSet(&worldEditor, "hidevols", ui_MenuItemCreate("Hide all volumes", UIMenuCheckRefButton, wleUIViewHideAllVols, NULL, &visSettings->hide_all_volumes));
	emMenuItemSet(&worldEditor, "hideoccvols", ui_MenuItemCreate("Hide occlusion volumes", UIMenuCheckRefButton, wleUIViewHideOccVols, NULL, &visSettings->hide_occlusion_volumes));
	emMenuItemSet(&worldEditor, "hideaudiovols", ui_MenuItemCreate("Hide audio volumes", UIMenuCheckRefButton, wleUIViewHideAudioVols, NULL, &visSettings->hide_audio_volumes));
	emMenuItemSet(&worldEditor, "hideskyvols", ui_MenuItemCreate("Hide skyfade volumes", UIMenuCheckRefButton, wleUIViewHideSkyVols, NULL, &visSettings->hide_skyfade_volumes));
	emMenuItemSet(&worldEditor, "hidehoodvols", ui_MenuItemCreate("Hide neighborhood volumes", UIMenuCheckRefButton, wleUIViewHideNeighborhoodVols, NULL, &visSettings->hide_neighborhood_volumes));
	emMenuItemSet(&worldEditor, "hideinteractionvols", ui_MenuItemCreate("Hide interaction volumes", UIMenuCheckRefButton, wleUIViewHideInteractionVols, NULL, &visSettings->hide_interaction_volumes));
	emMenuItemSet(&worldEditor, "hidelandmarkvols", ui_MenuItemCreate("Hide landmark volumes", UIMenuCheckRefButton, wleUIViewHideLandmarkVols, NULL, &visSettings->hide_landmark_volumes));
	emMenuItemSet(&worldEditor, "hidepowervols", ui_MenuItemCreate("Hide power volumes", UIMenuCheckRefButton, wleUIViewHidePowerVols, NULL, &visSettings->hide_power_volumes));
	emMenuItemSet(&worldEditor, "hidewarpvols", ui_MenuItemCreate("Hide warp volumes", UIMenuCheckRefButton, wleUIViewHideWarpVols, NULL, &visSettings->hide_warp_volumes));
	emMenuItemSet(&worldEditor, "hidegenesisvols", ui_MenuItemCreate("Hide genesis volumes", UIMenuCheckRefButton, wleUIViewHideGenesisVols, NULL, &visSettings->hide_genesis_volumes));
	emMenuItemSet(&worldEditor, "hideexclusionvols", ui_MenuItemCreate("Hide exclusion volumes", UIMenuCheckRefButton, wleUIViewHideExclusionVols, NULL, &visSettings->hide_exclusion_volumes));
	emMenuItemSet(&worldEditor, "hideuntypedvols", ui_MenuItemCreate("Hide volumes with no type", UIMenuCheckRefButton, wleUIViewHideUntypedVols, NULL, &visSettings->hide_untyped_volumes));
	emMenuItemSet(&worldEditor, "hideaggrovols", ui_MenuItemCreate("Hide aggro volumes", UIMenuCheckRefButton, wleUIViewHideAggroVols, NULL, &visSettings->hide_aggro_volumes));
	emMenuItemSet(&worldEditor, "hidepatrolpoints", ui_MenuItemCreate("Hide patrol points", UIMenuCheckButton, wleUIViewHidePatrolPoints, NULL, (void*) (intptr_t) EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePatrolPoints", 0)));
	emMenuItemSet(&worldEditor, "hideencounteractors", ui_MenuItemCreate("Hide encounter actors", UIMenuCheckButton, wleUIViewHideEncounterActors, NULL, (void*) (intptr_t) EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideEncounterActors", 0)));
	emMenuItemSet(&worldEditor, "disablevolcoll", ui_MenuItemCreate("Disable volume collision", UIMenuCheckRefButton, wleUIViewDisableVolColl, NULL, &editorUIState->disableVolColl));

	editorUIState->snapModeMenu = emMenuCreate(&worldEditor, "",
		"snapgrid",
		"snapvertex",
		"snapmidpoint",
		"snapedge",
		"snapface",
		"snapterrain",
		"snapsmart",
		"snapnone",
		NULL);
	emMenuItemSet(&worldEditor, "snapsubmenu", ui_MenuItemCreate("Snap to", UIMenuSubmenu, NULL, NULL, editorUIState->snapModeMenu));

	submenu = emMenuCreate(&worldEditor, "",
		"hidevols",
		"em_separator",
		"hideoccvols",
		"hideaudiovols",
		"hideskyvols",
		"hidehoodvols",
		"hideinteractionvols",
		"hidelandmarkvols",
		"hidepowervols",
		"hidewarpvols",
		"hidegenesisvols",
		"hideexclusionvols",
		"hideuntypedvols",
		"hideaggrovols",
		"em_separator",
		"disablevolcoll",
		NULL);
	emMenuItemSet(&worldEditor, "hidevolsubmenu", ui_MenuItemCreate("Hide volumes", UIMenuSubmenu, NULL, NULL, submenu));

	submenu = emMenuCreate(&worldEditor, "",
		"placeencounter",
		"placenamedpoint",
		"placepatrolroute",
		"placespawnpoint",
		"placetrigger",
		"placepathnode",
		NULL);

	emMenuItemSet(&worldEditor, "placeencobjsubmenu", ui_MenuItemCreate("Encounters", UIMenuSubmenu, NULL, NULL, submenu));

	submenu = emMenuCreate(&worldEditor, "",
		"placesound",
		"placevolume",
		"placewind",
		NULL);

	emMenuItemSet(&worldEditor, "placesystemobjsubmenu", ui_MenuItemCreate("System", UIMenuSubmenu, NULL, NULL, submenu));

	submenu = emMenuCreate(&worldEditor, "",
		"placebuilding",
		"placecurve",
		"placedebris",
		"placefxnode",
		"placeomnilight",
		"placeplanet",
		"placespotlight",
		NULL);

	emMenuItemSet(&worldEditor, "placeworldobjsubmenu", ui_MenuItemCreate("World", UIMenuSubmenu, NULL, NULL, submenu));

	submenu = emMenuCreate(&worldEditor, "",
		"placeencobjsubmenu",
		"placesystemobjsubmenu",
		"placeworldobjsubmenu",
		NULL);

	emMenuItemSet(&worldEditor, "placeobjsubmenu", ui_MenuItemCreate("Place", UIMenuSubmenu, NULL, NULL, submenu));

	// menu bar
	emMenuRegister(&worldEditor, emMenuCreate(&worldEditor, "File",
		"newlayer",
		"importlayer",
		"exportpositions",
		"exportgeometry",
		"setscene",
		"setpublicname",
		"setmaptype",
		"em_separator",
		"save",
		"saveas",
		"reloadfromsource",
		NULL));
	emMenuRegister(&worldEditor, emMenuCreate(&worldEditor, "Edit",
		"resetrot",
		"duplicate",
		"copy",
		"cut",
		"instance",
		"delete",
		"reseed",
		"findandreplace",
		"edittags",
		"em_separator",
		"placeobjsubmenu",
		"em_separator",
		"snapsubmenu",
		"snapnormal",
		"snapclamping",
		"em_separator",
		"lockfiles",
		"savetolib",
		NULL));
	emMenuRegister(&worldEditor, emMenuCreate(&worldEditor, "View",
		"focuscamera",
		"em_separator",
		"hide",
		"unhide",
		"em_separator",
		"hidevolsubmenu",
		"hidepatrolpoints",
		"hideencounteractors",
		NULL));
	emMenuRegister(&worldEditor, emMenuCreate(&worldEditor, "Select",
		//"invselect",
		"deselect",
		"lockselection",
		"em_separator",
		"freeze",
		"unfreeze",
		"lock",
		NULL));
	emMenuRegister(&worldEditor, emMenuCreate(&worldEditor, "Group",
		"group",
		"addtogroup",
		"ungroup",
		NULL));
	emMenuRegister(&worldEditor, emMenuCreate(&worldEditor, "Tools",
		"graphicsoptions",
		NULL));

	// custom menu
	if (!wleMenuRightClick)
	{
		wleMenuRightClick = StructCreate(parse_EdObjCustomMenu);
		wleMenuRightClick->editor = &worldEditor;
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("setdefaultparent"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("movetodefaultparent"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("copytoscratch"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("groupexpandall"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("em_separator"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("placeobjsubmenu"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("hidevolsubmenu"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("em_separator"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("duplicate"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("resetrot"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("reseed"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("rename"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("addtogroup"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("savetolib"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("em_separator"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("hide"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("unhide"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("freeze"));
		eaPush(&wleMenuRightClick->menuItems, StructAllocString("unfreeze"));
	}
	if(EditorPrefIsSet(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "CustomMenu"))
		EditorPrefGetStruct(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "CustomMenu", parse_EdObjCustomMenu, wleMenuRightClick);

	PERFINFO_AUTO_STOP();
}

/******
* CONTEXT MENU CALLBACKS
******/
// model editing menu item callback
static void editModelOpen(UIMenuItem *item, Model *model)
{
	char name[MAX_PATH+MAX_PATH];
	sprintf(name, "%s,%s", model->name, model->header->filename);
	emOpenFileEx(name, "model");
}

static void editModelOpenDirectory(UIMenuItem *item, Model *model)
{
	emuOpenContainingDirectory(model->header->filename);
}

static void editModelPreview(UIMenuItem *item, Model *model)
{
	PreviewResource(OBJECT_LIBRARY_DICT, model->name);
}


static void wleMenuBakeIntoTerrain(UIMenuItem *item, GroupTracker *tracker)
{
	terrainBakeTrackerIntoTerrain(tracker);
}

// material editing menu item callback
static void editMaterialOpen(UIMenuItem *item, MaterialData *materialData)
{
	emOpenFile(materialData->filename);
}

static void editMaterialOpenDirectory(UIMenuItem *item, MaterialData *materialData)
{
	emuOpenContainingDirectory(materialData->filename);
}

// texture editing menu item callback
static void editTextureOpen(UIMenuItem *item, BasicTexture *texture)
{
	char texFileName[CRYPTIC_MAX_PATH];
	sprintf(texFileName, "%s/%s", fileSrcDir(), texFindFullPath(texture));
	changeFileExt(texFileName, ".tga", texFileName);
	emuOpenFile(texFileName);
}

static void editTextureOpenDirectory(UIMenuItem *item, BasicTexture *texture)
{
	char texFileName[CRYPTIC_MAX_PATH];
	sprintf(texFileName, "%s/%s", fileSrcDir(), texFindFullPath(texture));
	changeFileExt(texFileName, ".tga", texFileName);
	emuOpenContainingDirectory(texFileName);
}

void wleMenuSetDefaultParentTracker(UIMenuItem *item, GroupTracker *tracker)
{
	TrackerHandle *handle = trackerHandleFromTracker(tracker);
	if (handle) wleSetDefaultParent(handle);
}

void wleMenuToggleSubObjects(UIMenuItem *item, GroupTracker *tracker)
{
	wleTrackerToggleEditSubObject(trackerHandleFromTracker(tracker));
}

static void wleMenuToggleAllSubObjectsRecurse(GroupTracker *tracker)
{
	int i;
	wleTrackerToggleEditSubObject(trackerHandleFromTracker(tracker));
	for (i = 0; i < tracker->child_count; i++)
		wleMenuToggleAllSubObjectsRecurse(tracker->children[i]);
}

void wleMenuToggleAllSubObjects(UIMenuItem *item, GroupTracker *tracker)
{
	EditUndoBeginGroup(edObjGetUndoStack());
	wleMenuToggleAllSubObjectsRecurse(tracker);
	EditUndoEndGroup(edObjGetUndoStack());
}

// context menu creation callback
void wleTrackerContextMenuCreateForModel(Model *model, UIMenuItem ***outItems)
{
	UIMenu *openSubMenu;
	UIMenuItem *namedItem, *item;
	char itemText[1024];
	char *c;

	if (!model || !model->header)
		return;

	openSubMenu = ui_MenuCreate(model->name);
	item = ui_MenuItemCreate("Open...", UIMenuCallback, editModelOpen, model, NULL);
	ui_MenuAppendItem(openSubMenu, item);
	sprintf(itemText, "Open \"%s\"...", model->header->filename);
	if (c = strrchr(itemText, '/'))
		*c = '\0';
	item = ui_MenuItemCreate(itemText, UIMenuCallback, editModelOpenDirectory, model, NULL);
	ui_MenuAppendItem(openSubMenu, item);
	item = ui_MenuItemCreate("Preview...", UIMenuCallback, editModelPreview, model, NULL);
	ui_MenuAppendItem(openSubMenu, item);

	sprintf(itemText, "Model: %s", model->name);
	namedItem = ui_MenuItemCreate(itemText, UIMenuSubmenu, NULL, NULL, (void*) openSubMenu);
	eaPush(outItems, namedItem);
}

void wleTrackerContextMenuCreateForMaterial(const Material *material, UIMenuItem ***outItems)
{
	const MaterialData *materialData = materialGetData(material);
	UIMenu *openSubMenu;
	UIMenuItem *namedItem, *item;
	char itemText[1024];
	char *c;

	if (!materialData)
		return;

	openSubMenu = ui_MenuCreate(material->material_name);
	item = ui_MenuItemCreate("Open...", UIMenuCallback, editMaterialOpen, (void*) materialData, NULL);
	ui_MenuAppendItem(openSubMenu, item);
	sprintf(itemText, "Open \"%s\"...", materialData->filename);
	if (c = strrchr(itemText, '/'))
		*c = '\0';
	item = ui_MenuItemCreate(itemText, UIMenuCallback, editMaterialOpenDirectory, (void*) materialData, NULL);
	ui_MenuAppendItem(openSubMenu, item);

	sprintf(itemText, "Material: %s", material->material_name);
	namedItem = ui_MenuItemCreate(itemText, UIMenuSubmenu, NULL, NULL, (void*) openSubMenu);
	eaPush(outItems, namedItem);
}

void wleTrackerContextMenuCreateForTexture(BasicTexture *texture, UIMenuItem ***outItems)
{
	UIMenu *openSubMenu;
	UIMenuItem *namedItem, *item;
	char itemText[1024];
	char dirnamebuf[MAX_PATH];

	if (!texture)
		return;

	openSubMenu = ui_MenuCreate(texGetName(texture));
	item = ui_MenuItemCreate("Open...", UIMenuCallback, editTextureOpen, texture, NULL);
	ui_MenuAppendItem(openSubMenu, item);
	sprintf(itemText, "Open \"%s\"...", texFindDirName(SAFESTR(dirnamebuf), texture));
	item = ui_MenuItemCreate(itemText, UIMenuCallback, editTextureOpenDirectory, texture, NULL);
	ui_MenuAppendItem(openSubMenu, item);

	sprintf(itemText, "Texture: %s", texGetName(texture));
	namedItem = ui_MenuItemCreate(itemText, UIMenuSubmenu, NULL, NULL, (void*) openSubMenu);
	eaPush(outItems, namedItem);
}

static int wleTrackerContextMenuAssocCmp(const MaterialTextureAssoc **assoc1, const MaterialTextureAssoc **assoc2)
{
	return strcmpi((*assoc1)->orig_name, (*assoc2)->orig_name);
}

static int wleTrackerContextMenuSwapCmp(const TextureSwap **swap1, const TextureSwap **swap2)
{
	return strcmpi((*swap1)->orig_name, (*swap2)->orig_name);
}

void wleTrackerContextMenuCreate(EditorObject *edObj, UIMenuItem ***outItems)
{
	UIMenuItem *item;
	UIMenu *submenu;
	MaterialTextureAssoc **matToTex = NULL;
	bool foundModels = false;
	int i;

	// model context menu
	if (edObj)
	{
		TrackerHandle *handle;
		GroupTracker *tracker;

		assert(edObj->type->objType == EDTYPE_TRACKER);
		handle = edObj->obj;
		tracker = trackerFromTrackerHandle(handle);

		if (tracker && tracker->def)
		{
			item = ui_MenuItemCreate("Set Default Parent", UIMenuCallback, wleMenuSetDefaultParentTracker, tracker, NULL);
			eaPush(outItems, item);

			if (tracker->def->property_structs.curve)
			{
				item = ui_MenuItemCreate("Toggle Subobjects", UIMenuCallback, wleMenuToggleSubObjects, tracker, NULL);
				eaPush(outItems, item);
			}
			item = ui_MenuItemCreate("Toggle All Subobjects", UIMenuCallback, wleMenuToggleAllSubObjects, tracker, NULL);
			eaPush(outItems, item);
			item = ui_MenuItemCreate("Bake Into Terrain", UIMenuCallback, wleMenuBakeIntoTerrain, tracker, NULL);
			eaPush(outItems, item);


			if (tracker->def->model)
			{
				eaPush(outItems, ui_MenuItemCreate("[Menu Separator]", UIMenuSeparator, NULL, NULL, NULL));
				wleTrackerContextMenuCreateForModel(tracker->def->model, outItems);
				foundModels = true;
			}

			wleGetTrackerTexMats(tracker, NULL, NULL, &matToTex, true);
		}
	}

	// material menus
	eaQSort(matToTex, wleTrackerContextMenuAssocCmp);
	for (i = 0; i < eaSize(&matToTex); i++)
	{
		UIMenuItem **items = NULL;
		char matSubmenuText[1024];
		int j;

		// add separator
		if (!i)
			eaPush(outItems, ui_MenuItemCreate("[Menu Separator]", UIMenuSeparator, NULL, NULL, NULL));

		// for each material, create submenu and menu item in the main context menu
		wleTrackerContextMenuCreateForMaterial(materialFind(matToTex[i]->orig_name, WL_FOR_WORLD), &items);
		submenu = ui_MenuCreate(matToTex[i]->orig_name);
		for (j = 0; j < eaSize(&items); j++)
			ui_MenuAppendItem(submenu, items[j]);

		// texture context submenu
		if (eaSize(&matToTex[i]->textureSwaps) > 0)
			ui_MenuAppendItem(submenu, ui_MenuItemCreate("[Menu Separator]", UIMenuSeparator, NULL, NULL, NULL));
		eaQSort(matToTex[i]->textureSwaps, wleTrackerContextMenuSwapCmp);

		for (j = 0; j < eaSize(&matToTex[i]->textureSwaps); j++)
		{
			BasicTexture *tex = texFind(matToTex[i]->textureSwaps[j]->orig_name, 1);
			int k;

			if (!tex)
				continue;

			eaClear(&items);
			wleTrackerContextMenuCreateForTexture(tex, &items);
			for (k = 0; k < eaSize(&items); k++)
				ui_MenuAppendItem(submenu, items[k]);
		}

		eaDestroy(&items);
		sprintf(matSubmenuText, "Material \"%s\"", matToTex[i]->orig_name);
		eaPush(outItems, ui_MenuItemCreate(matSubmenuText, UIMenuSubmenu, NULL, NULL, (void*) submenu));		
	}

	// cleanup
	eaDestroyStruct(&matToTex, parse_MaterialTextureAssoc);
}

#endif
