
#include "AnimList_Common.h"
#include "ClientTargeting.h"
#include "cutscene_common.h"
#include "EditLibUIUtil.h"
#include "EditLibGizmosToolbar.h"
#include "UIGimmeButton.h"
#include "EditorManager.h"
#include "EditorPrefs.h"
#include "Entity.h"
#include "oldencounter_common.h"
#include "CostumeCommonLoad.h"
#include "dynFxInfo.h"
#include "file.h"
#include "InputKeyBind.h"
#include "InputLib.h"
#include "gclCommandParse.h"
#include "gclCutscene.h"
#include "gclDemo.h"
#include "gclEntity.h"
#include "gfxPrimitive.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "GraphicsLib.h"
#include "SplineEditUI.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "WorldEditorUI.h"
#include "wlCurve.h"
#include "WorldGrid.h"
#include "soundLib.h"
#include "dynAnimChart.h"

#include "CutsceneDemoPlayEditor.h"

#include "cutscene_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static CutsceneDemoPlayEditor *s_pCutsceneDemoPlayEditor = NULL;

//////////////////////////////////////////////////////////////////////////
// Hotkey Callbacks
//////////////////////////////////////////////////////////////////////////

static void cutEdSaveDemoCB(UIButton *pButton, CutsceneEditorState *pState)
{
	char *pcDemoName;
	DemoRecording *pDemo;
	pDemo = demo_GetInfo(&pcDemoName);
	assert(pDemo);
	if(pDemo->cutsceneDef)
		StructDestroy(parse_CutsceneDef, pDemo->cutsceneDef);
	pDemo->cutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);
	if(ParserWriteTextFile(pcDemoName, parse_DemoRecording, pDemo, 0, 0))
	{
		pState->bUnsaved = 0;
		ui_ButtonSetText(pButton, "Save");

		// Clear the undo stack on revert
		EditUndoStackClear(pState->edit_undo_stack);
		StructDestroy(parse_CutsceneDef, pState->pNextUndoCutsceneDef);
		pState->pNextUndoCutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);
	}
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.Save") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdSaveAC()
{
#ifndef NO_EDITORS
	if(!cutEdDemoPlayEditor())
		return;
	cutEdSaveDemoCB(cutEdDemoPlayEditor()->state.pUIButtonSaveDemo, &cutEdDemoPlayEditor()->state);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.Undo") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdUndoAC()
{
#ifndef NO_EDITORS
	if(!cutEdDemoPlayEditor())
		return;
	EditUndoLast(cutEdDemoPlayEditor()->state.edit_undo_stack);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.Redo") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdRedoAC()
{
#ifndef NO_EDITORS
	if(!cutEdDemoPlayEditor())
		return;
	EditRedoLast(cutEdDemoPlayEditor()->state.edit_undo_stack);
#endif
}

#ifndef NO_EDITORS

static void cutEdShowWindow()
{
	if(!cutEdDemoPlayEditor())
		return;
	
	ui_WidgetGroupAdd(ui_PaneWidgetGroupForDevice(NULL), UI_WIDGET( cutEdDemoPlayEditor()->state.pTimelinePane ));
	ui_WidgetGroupAdd(ui_PaneWidgetGroupForDevice(NULL), UI_WIDGET( cutEdDemoPlayEditor()->pMainPane ));
}

static void cutEdHideWindow()
{
	if(!cutEdDemoPlayEditor())
		return;

	ui_WidgetRemoveFromGroup( UI_WIDGET( cutEdDemoPlayEditor()->state.pTimelinePane ));
	ui_WidgetRemoveFromGroup( UI_WIDGET( cutEdDemoPlayEditor()->pMainPane ));
}

#endif

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.Toggle") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdToggleAC()
{
#ifndef NO_EDITORS
	if(!cutEdDemoPlayEditor())
		return;
	if(cutEdDemoPlayEditor()->pMainPane->widget.group)
		cutEdHideWindow();
	else
		cutEdShowWindow();
#endif
}

#ifndef NO_EDITORS

static void cutsceneDemoPlayEditorAddToCont(UIExpander *pExpander, UIAnyWidget *widget)
{
	ui_ExpanderAddChild(pExpander, widget);
	ui_ExpanderSetHeight(pExpander, ui_WidgetGetNextY(widget)+CSE_UI_GAP);
}

static void cutsceneDemoPlayEditorAddCont(CutsceneEditorState *pState, UIExpander *pExpander)
{
	ui_ExpanderGroupAddExpander(pState->pExpanderGroup, pExpander);
}

static void cutsceneDemoPlayEditorRemoveCont(CutsceneEditorState *pState, UIExpander *pExpander)
{
	ui_ExpanderGroupRemoveExpander(pState->pExpanderGroup, pExpander);
}

static void cutEdSetEditMode(CutsceneEditMode mode)
{
	if(cutEdDemoPlayEditor())
		ui_ComboBoxSetSelectedEnumAndCallback(cutEdDemoPlayEditor()->state.pUIComboEditMode, mode);
}

static void cutsceneDemoPlayEditorInitData(CutsceneEditorState *pState)
{
	pState->pInsertIntoContFunc = cutsceneDemoPlayEditorAddToCont;
	pState->pAddContFunc = cutsceneDemoPlayEditorAddCont;
	pState->pRemoveContFunc = cutsceneDemoPlayEditorRemoveCont;
	pState->pFileCont = ui_ExpanderCreate("File", 0);
	ui_ExpanderSetOpened(pState->pFileCont, true);
	pState->pPreviewCont = ui_ExpanderCreate("Preview", 0);
	ui_ExpanderSetOpened(pState->pPreviewCont, true);
	pState->pPathListCont = ui_ExpanderCreate("Path List", 0);
	ui_ExpanderSetOpened(pState->pPathListCont, true);
	pState->pPointListCont = ui_ExpanderCreate("Point List", 0);
	ui_ExpanderSetOpened(pState->pPointListCont, true);
	pState->pBasicPathCont = ui_ExpanderCreate("Basic Path", 0);
	ui_ExpanderSetOpened(pState->pBasicPathCont, true);
	pState->pCirclePathCont = ui_ExpanderCreate("Circle Path", 0);
	ui_ExpanderSetOpened(pState->pCirclePathCont, true);
	pState->pWatchPathCont = ui_ExpanderCreate("Entity Path", 0);
	ui_ExpanderSetOpened(pState->pWatchPathCont, true);
	pState->pPointCont = ui_ExpanderCreate("Selected Point", 0);
	ui_ExpanderSetOpened(pState->pPointCont, true);
	pState->pRelativePosCont = ui_ExpanderCreate("Positioning", 0);
	ui_ExpanderSetOpened(pState->pRelativePosCont, true);
	pState->pGenPntCont = ui_ExpanderCreate("Point Common", 0);
	ui_ExpanderSetOpened(pState->pGenPntCont, true);	
	pState->pGenCGTCont = ui_ExpanderCreate("Path Common", 0);
	ui_ExpanderSetOpened(pState->pGenCGTCont, true);	
	pState->pUISetEditModeFunc = cutEdSetEditMode;
}

static void cutEdPaneTick(UIPane *pPane, UI_PARENT_ARGS)
{
	ui_PaneTick(pPane, UI_PARENT_VALUES);

	cutEdTick(&cutEdDemoPlayEditor()->state);
	cutEdDraw(&cutEdDemoPlayEditor()->state);
}

#endif

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.Open") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdOpenWindow(bool dontShow)
{
#ifndef NO_EDITORS
	if(!demo_playingBack())
		return;

	if(!cutEdDemoPlayEditor())
	{
		DemoRecording *pDemo;
		int w, h;
		int y = 0;
		UIPane *pPane;
		UIExpanderGroup *pExpanderGroup;
		s_pCutsceneDemoPlayEditor = calloc(1, sizeof(CutsceneDemoPlayEditor));

		pDemo = demo_GetInfo(NULL);
		assert(pDemo);

		if(pDemo->cutsceneDef)
		{
			s_pCutsceneDemoPlayEditor->state.pCutsceneDef = StructClone(parse_CutsceneDef, pDemo->cutsceneDef);
		}
		else
		{
			//ShawnF TODO: this should become an import
			s_pCutsceneDemoPlayEditor->state.pCutsceneDef = StructCreate(parse_CutsceneDef);
			s_pCutsceneDemoPlayEditor->state.pCutsceneDef->iVersion = CUTSCENE_DEF_VERSION;
		}
		assert(s_pCutsceneDemoPlayEditor->state.pCutsceneDef);
		if(!s_pCutsceneDemoPlayEditor->state.pCutsceneDef->pPathList)
		{
			s_pCutsceneDemoPlayEditor->state.pCutsceneDef->pPathList = StructCreate(parse_CutscenePathList);
			s_pCutsceneDemoPlayEditor->state.pCutsceneDef->fBlendRate = CSE_DEFAULT_BLEND_RATE;
		}
		else
		{
			gclCutsceneLoadSplines(s_pCutsceneDemoPlayEditor->state.pCutsceneDef->pPathList);
		}
		s_pCutsceneDemoPlayEditor->state.pNextUndoCutsceneDef = StructClone(parse_CutsceneDef, s_pCutsceneDemoPlayEditor->state.pCutsceneDef);

		//ShawnF TODO add prefs
		//EditorPrefStoreFloat(TER_ED_NAME, (brush ? brush->name : MULTI_BRUSH_NAME), "Diameter", common->brush_diameter);

		gfxGetActiveDeviceSize(&w, &h);
		//pPane = ui_WindowCreate("Demo Editor", 20, 200, 300, (h / g_ui_State.scale)-40);
		pPane = ui_PaneCreate( 0, 0, 350, 1, UIUnitFixed, UIUnitPercentage, UI_PANE_VP_LEFT );
		pPane->widget.priority = 75;
		pPane->widget.offsetFrom = UITopRight;
		s_pCutsceneDemoPlayEditor->pMainPane = pPane;

		pExpanderGroup = ui_ExpanderGroupCreate();
		ui_WidgetSetPosition(UI_WIDGET(pExpanderGroup), 0, 5);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pExpanderGroup), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
		//ui_ExpanderGroupSetGrow(pExpanderGroup, true);
		ui_PaneAddChild(pPane, UI_WIDGET(pExpanderGroup));
		s_pCutsceneDemoPlayEditor->state.pExpanderGroup = pExpanderGroup;
  
		cutsceneDemoPlayEditorInitData(&s_pCutsceneDemoPlayEditor->state);
		cutEdInitCommon(&s_pCutsceneDemoPlayEditor->state, s_pCutsceneDemoPlayEditor->state.pCutsceneDef);
		cutEdInitUICommon(&s_pCutsceneDemoPlayEditor->state, s_pCutsceneDemoPlayEditor->state.pCutsceneDef);
		s_pCutsceneDemoPlayEditor->state.pTimelinePane->widget.priority = 50;
		UI_WIDGET(s_pCutsceneDemoPlayEditor->state.pTimelinePane)->tickF = cutEdPaneTick;

		s_pCutsceneDemoPlayEditor->state.edit_undo_stack = EditUndoStackCreate();
		EditUndoSetContext(s_pCutsceneDemoPlayEditor->state.edit_undo_stack, &s_pCutsceneDemoPlayEditor->state);

		keybind_PushProfileName("CutsceneEditor");
		keybind_PushProfileName("CutsceneDemoEditor");
		if( !dontShow )
			cutEdShowWindow();
	}
	else
	{
		if(!cutEdDemoPlayEditor()->pMainPane->widget.group)
			if(!dontShow)
				cutEdShowWindow();
	}
#endif
}

void cutEdCloseWindow()
{
#ifndef NO_EDITORS
	cutEdHideWindow();
#endif
}

CutsceneDemoPlayEditor *cutEdDemoPlayEditor()
{
#ifndef NO_EDITORS
	return s_pCutsceneDemoPlayEditor;
#else
	return NULL;
#endif
}

CutsceneEditorState* cutEdDemoPlaybackState()
{
#ifndef NO_EDITORS
	return SAFE_MEMBER_ADDR(cutEdDemoPlayEditor(), state);
#else
	return NULL;
#endif
}
