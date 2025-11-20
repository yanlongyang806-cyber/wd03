//
// SkyEditor2.c
//

#include "SkyEditor2.h"

#include "EditLibUIUtil.h"
#include "GfxEditorIncludes.h"
#include "GfxDebug.h"
#include "GfxPrimitive.h"
#include "Materials.h"
#include "MultiEditFieldContext.h"
#include "gimmeDLLWrapper.h"
#include "TokenStore.h"
#include "structHist.h"
#include "UIColorButton.h"
#include "StringUtil.h"
#include "wlGroupPropertyStructs.h"
#include "UIGraph.h"
#include "UIGimmeButton.h"
#include "rgb_hsv.h"
#include "WorldEditorAppearanceAttributes.h"
#include "WorldGrid.h"
#include "WorldBounds.h"

#include "AutoGen/SkyEditor2_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define SE_TrackTimeToSky(x) ((x)/1000.0f)
#define SE_SkyTimeToTrack(x) ((x)*1000.0f)

#define EDITOR_ROW_HEIGHT		28
#define EDITOR_ROW_HALF_HEIGHT	14
#define EDITOR_LEFT_PAD			5
#define EDITOR_RIGHT_PAD		5
#define EDITOR_CONTROL_X		135

typedef struct SEUndoData 
{
	SkyInfo *pPreSkyInfo;
	SkyInfo *pPostSkyInfo;
} SEUndoData;

static SkyViewType g_SkyViewType = 0;

static void skyEdRefreshUI(SkyEditorDoc *pDoc);
static void skyEdSetUnsaved(SkyEditorDoc *pDoc);
static SkyBlockInfo* skyEdGetSelectedSkyDomeInfo(SkyEditorDoc *pDoc);
static void skyEdSetDomeDisplayNameUnique(SkyEditorDoc *pDoc, SkyDome *pDomeIn, const char *pcName);
static void skyEdAddSkyNewSkyBlockTime(UITimeline *pTrack, int timelineTime, SkyBlockInfo *pInfo);
static void skyEdRegisterSkyDomes(SkyEditorDoc *pDoc);
typedef void (*skyEdRunOnItemFunc)(SkyEditorDoc *pDoc, int idx, void *pOrig);
static void skyEdRefreshCopyPasteEX( SkyEditorDoc *pDoc, const char *pcGroupName, ParseTable *pParseTable, void *pSkyBlock, UIWidget *pParent, int *y_out );

#define SE_SetTextFromFloat(txt, flt) {char buf[64]; sprintf(buf, "%0.2f", (flt)); ui_TextEntrySetText((txt), buf);}

//////////////////////////////////////////////////////////////////////////

static bool skyEdIsEditable(SkyEditorDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if(pDoc && !emDocIsEditable(&pDoc->emDoc, true))
	{
		return false;
	}
	return true;
}

static void skyEdSetUpWidget(UIWidget *pWidget, UIWidget *pParent)
{
	ui_WidgetSetWidthEx(pWidget, 1, UIUnitPercentage);
	ui_WidgetAddChild(pParent, pWidget);
	ui_WidgetSetPaddingEx(pWidget, 0, EDITOR_RIGHT_PAD, 0, 0);
}

static void skyEdMakeOpaqueLabel(UILabel *pLabel, int red, int green, int blue)
{
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1, UIUnitPercentage);
	pLabel->bOpaque = true;
	pLabel->widget.pOverrideSkin = NULL;
	setVec3(pLabel->widget.color[0].rgb, red, green, blue);
}

static SkyDome* skyEdGetSkyDomeFromInfoEx(SkyBlockInfo *pInfo, SkyInfo *pSkyInfo)
{
	if(pInfo->iSkyDomeIdx >= 0 && pInfo->iSkyDomeIdx < eaSize(&pSkyInfo->skyDomes))
		return pSkyInfo->skyDomes[pInfo->iSkyDomeIdx];
	return NULL;
}

static SkyDome* skyEdGetSkyDomeFromInfo(SkyBlockInfo *pInfo)
{
	return skyEdGetSkyDomeFromInfoEx(pInfo, pInfo->pDoc->pSkyInfo);
}

static GenericSkyBlock*** skyEdGetInfoTimesEx(SkyBlockInfo *pInfo, SkyInfo *pSkyInfo)
{
	if(pInfo->bIsSkyDome) {
		SkyDome *pDome = skyEdGetSkyDomeFromInfoEx(pInfo, pSkyInfo);
		if(pDome)
			return (GenericSkyBlock***)(&pDome->dome_values);
		return NULL;
	}
	return (GenericSkyBlock***)TokenStoreGetEArray(parse_SkyInfo, pInfo->iColumn, pSkyInfo, NULL);;
}

static GenericSkyBlock*** skyEdGetInfoTimes(SkyBlockInfo *pInfo)
{
	return skyEdGetInfoTimesEx(pInfo, pInfo->pDoc->pSkyInfo);
}


typedef struct {
	SkyEditorDoc *pDoc;
	ParseTable *skyType;
	void *pSkyBlock;
} skyEdCopyBlock;

static skyEdCopyBlock *skyEdClipBoard = NULL;

void skyEdDestroySkyBlockInfo(SkyBlockInfo *pInfo)
{
	int i;
	SkyEditorDoc *pDoc = pInfo->pDoc;

	for ( i=0; i < eaSize(&pDoc->UI.ppElementList); i++ ) {
		SkyEditorUIElement *pElem = pDoc->UI.ppElementList[i];
		if(pElem->pcBlockInfo == pInfo->pcDisplayName) {
			MEFieldSafeDestroy(&pElem->pField);
			if(pElem->pLabel) {
				ui_WidgetQueueFree(UI_WIDGET(pElem->pLabel));
				pElem->pLabel = NULL;
			}
		}
	}

	ui_TimelineRemoveTrack(pDoc->UI.pTimeline, pInfo->pTrack);
	ui_TimelineTrackFree(pInfo->pTrack);
	eaFindAndRemove(&pDoc->emDoc.em_panels, pInfo->pPanel);
	emPanelFree(pInfo->pPanel);
	eaDestroy(&pInfo->ppSelected);
	eaiDestroy(&pInfo->ipPrevSelected);

	free(pInfo);
}

static void skyEdRemoveCopyPasteSkyBlockMenuButton();

void skyEdReRegisterSkyDomes(SkyEditorDoc *pDoc)
{
	int i;
	for ( i=eaSize(&pDoc->ppBlockInfos)-1; i >=0 ; i-- ) {
		SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
		if(pInfo->bIsSkyDome) {
			eaRemove(&pDoc->ppBlockInfos, i);
			skyEdDestroySkyBlockInfo(pInfo);
		}
	}

	// remove from group so we do not crash later. COR-15504
	skyEdRemoveCopyPasteSkyBlockMenuButton();

	skyEdRegisterSkyDomes(pDoc);
}

static void skyEdClearSelection(SkyEditorDoc *pDoc)
{
	int i;
	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		eaClear(&pDoc->ppBlockInfos[i]->ppSelected);
	}
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("SkyEditor.ClearSelection") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void skyEdClearSelectionAC()
{
	SkyEditorDoc *pDoc = (SkyEditorDoc*)emGetActiveEditorDoc();

	if(!pDoc)
		return;

	skyEdClearSelection(pDoc);
	skyEdRefreshUI(pDoc);
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("SkyEditor.Delete") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void skyEdDeleteAC()
{
	int i, j;
	bool bSkyDomesDeleted = false;
	SkyEditorDoc *pDoc = (SkyEditorDoc*)emGetActiveEditorDoc();

	if(!pDoc)
		return;

	if(!skyEdIsEditable(pDoc))
		return;

	for ( i=eaSize(&pDoc->ppBlockInfos)-1; i >= 0 ; i-- ) {
		SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
		GenericSkyBlock ***pppBlocks = skyEdGetInfoTimes(pInfo);

		for ( j=0; j < eaSize(&pInfo->ppSelected); j++ ) {
			GenericSkyBlock *pSelected = pInfo->ppSelected[j];
			eaFindAndRemove(pppBlocks, pSelected);
			StructDestroyVoid(pInfo->pBlockPti, pSelected);
		}
		eaClear(&pInfo->ppSelected);

		if(pInfo->bIsSkyDome && eaSize(pppBlocks)==0) {
			SkyDome *pDome = skyEdGetSkyDomeFromInfoEx(pInfo, pDoc->pSkyInfo);
			assert(pDome);
			eaRemove(&pDoc->pSkyInfo->skyDomes, pInfo->iSkyDomeIdx);
			StructDestroy(parse_SkyDome, pDome);
			bSkyDomesDeleted = true;
		}
	}

	if(bSkyDomesDeleted)
		skyEdReRegisterSkyDomes(pDoc);

	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
}

static void skyEdStoreSelection(SkyEditorDoc *pDoc)
{
	int i, j;
	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
		GenericSkyBlock **ppBlocks = *skyEdGetInfoTimes(pInfo);

		eaiClear(&pInfo->ipPrevSelected);
		for ( j=0; j < eaSize(&pInfo->ppSelected); j++ ) {
			int idx = eaFind(&ppBlocks, pInfo->ppSelected[j]);
			eaiPush(&pInfo->ipPrevSelected, idx);
		}
	}

	{
		SkyBlockInfo *pDomeInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
		if(pDomeInfo) {
			pDoc->iPrevSelectedSkyDomeIdx = pDomeInfo->iSkyDomeIdx;
			eaiCopy(&pDoc->ipPrevSelectedSkyDomeTimes, &pDomeInfo->ipPrevSelected);
		} else {
			pDoc->iPrevSelectedSkyDomeIdx = -1;
			eaiClear(&pDoc->ipPrevSelectedSkyDomeTimes);
		}
	}
}

static void skyEdRestoreSelection(SkyEditorDoc *pDoc)
{
	int i, j;
	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
		GenericSkyBlock **ppBlocks = *skyEdGetInfoTimes(pInfo);

		if(pInfo->bIsSkyDome && pInfo->iSkyDomeIdx == pDoc->iPrevSelectedSkyDomeIdx)
			eaiCopy(&pInfo->ipPrevSelected, &pDoc->ipPrevSelectedSkyDomeTimes);

		eaClear(&pInfo->ppSelected);
		for ( j=0; j < eaiSize(&pInfo->ipPrevSelected); j++ ) {
			int idx = pInfo->ipPrevSelected[j];
			if(idx >= 0 && idx < eaSize(&ppBlocks))
				eaPush(&pInfo->ppSelected, ppBlocks[idx]);
		}
	}
}

static void skyEdUndoCB(SkyEditorDoc *pDoc, SEUndoData *pData)
{
	skyEdStoreSelection(pDoc);
	skyEdClearSelection(pDoc);

	StructDestroy(parse_SkyInfo, pDoc->pSkyInfo);
	pDoc->pSkyInfo = StructClone(parse_SkyInfo, pData->pPreSkyInfo);
	StructDestroySafe(parse_SkyInfo, &pDoc->pNextUndoSkyInfo);
	pDoc->pNextUndoSkyInfo = StructClone(parse_SkyInfo, pDoc->pSkyInfo);
	assert(pDoc->pSkyInfo);

	skyEdReRegisterSkyDomes(pDoc);
	skyEdRestoreSelection(pDoc);
	skyEdRefreshUI(pDoc);
}

static void skyEdRedoCB(SkyEditorDoc *pDoc, SEUndoData *pData)
{
	skyEdStoreSelection(pDoc);
	skyEdClearSelection(pDoc);

	StructDestroy(parse_SkyInfo, pDoc->pSkyInfo);
	pDoc->pSkyInfo = StructClone(parse_SkyInfo, pData->pPostSkyInfo);
	StructDestroySafe(parse_SkyInfo, &pDoc->pNextUndoSkyInfo);
	pDoc->pNextUndoSkyInfo = StructClone(parse_SkyInfo, pDoc->pSkyInfo);
	assert(pDoc->pSkyInfo);

	skyEdReRegisterSkyDomes(pDoc);
	skyEdRestoreSelection(pDoc);
	skyEdRefreshUI(pDoc);
}

static void skyEdUndoFreeCB(SkyEditorDoc *pDoc, SEUndoData *pData)
{
	// Free the memory
	StructDestroy(parse_SkyInfo, pData->pPreSkyInfo);
	StructDestroy(parse_SkyInfo, pData->pPostSkyInfo);
	free(pData);
}

static void skyEdSetUnsaved(SkyEditorDoc *pDoc)
{
	SEUndoData *pData = calloc(1, sizeof(SEUndoData));
	pData->pPreSkyInfo = pDoc->pNextUndoSkyInfo;
	pData->pPostSkyInfo = StructClone(parse_SkyInfo, pDoc->pSkyInfo);

	EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, skyEdUndoCB, skyEdRedoCB, skyEdUndoFreeCB, pData);
	emSetDocUnsaved(&pDoc->emDoc, false);

	pDoc->pNextUndoSkyInfo = StructClone(parse_SkyInfo, pDoc->pSkyInfo);
}

static int skyEdSortBlocks(const GenericSkyBlock **left, const GenericSkyBlock **right)
{
	return ((*left)->time > (*right)->time) ? 1 : -1;
}

static bool skyEdTimelineFramePreChangedCB(UITimelineKeyFrame *pFrame, SkyEditorDoc *pDoc)
{
	return skyEdIsEditable(pDoc);
}

static bool skyEdTimelinePreChangedCB(UITimeline *pTimeline, SkyEditorDoc *pDoc)
{
	return skyEdIsEditable(pDoc);
}

static void skyEdTimelineTimeChangedCB(UITimeline *pTimeline, int time, SkyEditorDoc *pDoc)
{
	skyEdRefreshUI(pDoc);
}

static void skyEdTimelineBlockTimesChangedCB(UITimeline *pTimeline, SkyEditorDoc *pDoc)
{
	int i;

	if(!skyEdIsEditable(pDoc))
		return;

	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		GenericSkyBlock **ppData = *skyEdGetInfoTimes(pDoc->ppBlockInfos[i]);
		eaQSort(ppData, skyEdSortBlocks);
	}
	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
}

static void skyEdTimelineSelectionChangedCB(UITimeline *pTimeline, SkyEditorDoc *pDoc)
{
	int i, j;
	bool bFound = false;
	GenericSkyBlock *pSelected = NULL;
	skyEdClearSelection(pDoc);
	for ( i=0; i < eaSize(&pTimeline->selection); i++ ) {
		UITimelineKeyFrame *pKeyFrame = pTimeline->selection[i];
		for ( j=0; j < eaSize(&pDoc->ppBlockInfos); j++ ) {
			GenericSkyBlock **ppData = *skyEdGetInfoTimes(pDoc->ppBlockInfos[j]);
			int dataIdx = eaFind(&ppData, pKeyFrame->data);
			if(dataIdx >= 0) {
				eaPush(&pDoc->ppBlockInfos[j]->ppSelected, ppData[dataIdx]);
				if(!bFound) {
					pSelected = ppData[dataIdx];
					bFound = true;
				} else if (pSelected) {
					pSelected = NULL;
				}
				break;
			}
		}
	}

	//True if only one item is selected
	if(pSelected)
		ui_TimelineSetTime(pTimeline, SE_SkyTimeToTrack(pSelected->time));

	skyEdRefreshUI(pDoc);
}

typedef struct skyEdNewBlockWin
{
	UIWindow *pWindow;
	SkyEditorDoc *pDoc;

	const char **ppInfoStrings;
	UIComboBox *pTypeCombo;

} skyEdNewBlockWin;

static bool skyEdNewBlockClose(UIButton *pUnused, skyEdNewBlockWin *pUI)
{
	elUIWindowClose(NULL, pUI->pWindow);
	ui_ComboBoxSetPopupOpen(pUI->pTypeCombo, false);
	pUI->pTypeCombo->model = NULL;
	ui_WidgetQueueFree(UI_WIDGET(pUI->pTypeCombo));
	eaDestroy(&pUI->ppInfoStrings);
	SAFE_FREE(pUI);
	return true;
}

static void skyEdNewBlockOK(UIButton *pUnused, skyEdNewBlockWin *pUI)
{
	int i;
	int idx = ui_ComboBoxGetSelected(pUI->pTypeCombo);
	SkyEditorDoc *pDoc = pUI->pDoc;
	const char *pcSelected;

	if(!skyEdIsEditable(pDoc))
		return;

	assert(idx >= 0 && idx < eaSize(&pUI->ppInfoStrings));
	pcSelected = pUI->ppInfoStrings[idx];

	if(pcSelected == "Sky Dome") {
		SkyDome *pDome = StructCreate(parse_SkyDome);
		SkyDomeTime *pDomeTime = StructCreate(parse_SkyDomeTime);
		const char *pcDisplayName;
		eaPush(&pDoc->pSkyInfo->skyDomes, pDome);
		eaPush(&pDome->dome_values, pDomeTime);
		
		pDome->name = StructAllocString("Sys_Unit_Quad");
		pDome->rotation_axis[2] = 1.0f;
		skyEdSetDomeDisplayNameUnique(pDoc, pDome, NULL);
		pDomeTime->alpha = 1.0;
		pDomeTime->scale = 1.0;
		pDomeTime->tintHSV[2] = 1.0f;

		skyEdClearSelection(pDoc);
		skyEdReRegisterSkyDomes(pDoc);

		//Select the new time
		pcDisplayName = allocAddString(pDome->display_name);
		for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
			SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
			if(pInfo->bIsSkyDome && pInfo->pcDisplayName == pcDisplayName) {
				eaPush(&pInfo->ppSelected, (GenericSkyBlock*)pDomeTime);
			}
		}

		skyEdRefreshUI(pDoc);
		skyEdSetUnsaved(pDoc);	
	} else {
		SkyBlockInfo *pSelectedInfo = NULL;
		for ( i=0; i < eaSize(&pUI->pDoc->ppBlockInfos); i++ ) {
			SkyBlockInfo *pInfo = pUI->pDoc->ppBlockInfos[i];
			if(pInfo->pcDisplayName == pcSelected) {
				pSelectedInfo = pInfo;
				break;
			}
		}
		assert(pSelectedInfo);
		skyEdAddSkyNewSkyBlockTime(NULL, 0, pSelectedInfo);
	}

	skyEdNewBlockClose(NULL, pUI);
}

static void skyEdPlayCB(void *pUnused, SkyEditorDoc *pDoc)
{
	pDoc->fPlaySpeed = 1;
}

static void skyEdFastForwardCB(void *pUnused, SkyEditorDoc *pDoc)
{
	pDoc->fPlaySpeed = MAX(pDoc->fPlaySpeed, 1);
	pDoc->fPlaySpeed *= 4;
	pDoc->fPlaySpeed = MIN(pDoc->fPlaySpeed, 256);
}

static void skyEdPauseCB(void *pUnused, SkyEditorDoc *pDoc)
{
	pDoc->fPlaySpeed = 0;
}

static void skyEdNewBlockCB(void *pUnused, SkyEditorDoc *pDoc)
{
	int i;
	skyEdNewBlockWin *pUI;
	UIWindow *pWin;
	UILabel *pLabel;
	UIButton *pButton;
	UIComboBox *pCombo;
	int y = 5;

	if(!skyEdIsEditable(pDoc))
		return;

	pUI = calloc(1, sizeof(*pUI));
	pUI->pDoc = pDoc;

	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
		if(!pInfo->bIsSkyDome) {
			GenericSkyBlock ***pppTimes = skyEdGetInfoTimes(pInfo);
			if(eaSize(pppTimes) == 0) {
				eaPush(&pUI->ppInfoStrings, pInfo->pcDisplayName);
			}
		}
	}
	eaPush(&pUI->ppInfoStrings, "Sky Dome");

	pWin = ui_WindowCreate("New Sky Block", 100, 100, 300, 90);
	pUI->pWindow = pWin;

	pLabel = ui_LabelCreate("Block Type:", 5, y);
	ui_WindowAddChild(pWin, pLabel);
	pCombo = ui_ComboBoxCreate(100, y, 1, NULL, &pUI->ppInfoStrings, NULL);
	ui_ComboBoxSetSelected(pCombo, 0);
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1, UIUnitPercentage);
	ui_WindowAddChild(pWin, pCombo);
	pUI->pTypeCombo = pCombo;
	y += EDITOR_ROW_HEIGHT;

	pButton = elUIAddCancelOkButtons(pWin, skyEdNewBlockClose, pUI, skyEdNewBlockOK, pUI);
	y += EDITOR_ROW_HEIGHT;
	ui_WindowSetCloseCallback(pWin, skyEdNewBlockClose, pUI);
	ui_WidgetSetHeight(UI_WIDGET(pWin), y);
	elUICenterWindow(pWin);
	ui_WindowSetModal(pWin, true);
	ui_WindowSetResizable(pWin, false);
	ui_WindowShow(pWin);
}

static void skyEdSetSkyOverrides(SkyEditorDoc* pDoc)
{
	int i;
	GfxCameraController *pCamera = emGetWorldCamera();
	WorldRegion *pRegion = worldGetWorldRegionByPos(pCamera->camcenter);
	SkyInfoGroup *pSkyGroup = pRegion ? worldRegionGetSkyGroup(pRegion) : NULL;
	bool bFound = false;
	const char **ppOverrideList = NULL;

	
	if(g_SkyViewType == SVT_JustEdit || !pSkyGroup) {
		eaPush(&ppOverrideList, "default_sky");
	} else {
		for ( i=0; i < eaSize(&pSkyGroup->override_list); i++ ) {
			SkyInfoOverride *pOverride = pSkyGroup->override_list[i];
			SkyInfo *pSky = GET_REF(pOverride->sky);
			if(pSky->filename_no_path == pDoc->pSkyInfo->filename_no_path) {
				if(g_SkyViewType != SVT_JustWorld) {
					bFound = true;
					eaPush(&ppOverrideList, pSky->filename_no_path);
				}
			} else {
				eaPush(&ppOverrideList, pSky->filename_no_path);
			}
		}
	}

	if(g_SkyViewType != SVT_JustWorld && !bFound) {
		eaPush(&ppOverrideList, pDoc->pSkyInfo->filename_no_path);
	}

	gfxCameraControllerSetSkyGroupOverride(pCamera, ppOverrideList, NULL);
	eaDestroy(&ppOverrideList);
}

static void skyEdInitTimelineUI(SkyEditorDoc *pDoc)
{
	UIPane *pPane;
	UITimeline *pTimeline;
	UIButton *pButton;
	
	pPane = ui_PaneCreate( 0, 0, 1, 200, UIUnitPercentage, UIUnitFixed, UI_PANE_VP_TOP );
	ui_PaneSetResizable(pPane, UITop, 0, 110);
	pPane->widget.offsetFrom = UIBottomLeft;
	pDoc->UI.pTimelinePane = pPane;

	pTimeline = ui_TimelineCreate(0,0,1);
	ui_WidgetSetDimensionsEx( UI_WIDGET( pTimeline ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_TimelineSetTimeChangedCallback(pTimeline, skyEdTimelineTimeChangedCB, pDoc);
	ui_TimelineSetFramePreChangedCallback(pTimeline, skyEdTimelinePreChangedCB, pDoc);
	ui_TimelineSetFrameChangedCallback(pTimeline, skyEdTimelineBlockTimesChangedCB, pDoc);
	ui_TimelineSetSelectionChangedCallback(pTimeline, skyEdTimelineSelectionChangedCB, pDoc);
	pTimeline->continuous = true;
	pTimeline->max_time_mode = true;
	pTimeline->limit_zoom_out = true;
	pTimeline->zoomed_out = true;
	pTimeline->total_time = 24500;
	pTimeline->time_ticks_in_units = true;
	pDoc->UI.pTimeline = pTimeline;

	#define SE_UI_PLAY_BUTTON_HEIGHT 20
	#define SE_UI_PLAY_BUTTON_WIDTH 25
	#define SE_UI_PLAY_IMAGE_HEIGHT 15
	#define SE_UI_PLAY_IMAGE_WIDTH 15

	pButton = ui_ButtonCreateImageOnly("play_icon", 0, 5, skyEdPlayCB, pDoc);
	ui_ButtonSetImageStretch(pButton, true);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), SE_UI_PLAY_BUTTON_WIDTH, SE_UI_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	pButton = ui_ButtonCreateImageOnly("ff_icon", ui_WidgetGetNextX(UI_WIDGET(pButton)) + 5, 5, skyEdFastForwardCB, pDoc);
	ui_ButtonSetImageStretch(pButton, true);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), SE_UI_PLAY_BUTTON_WIDTH, SE_UI_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	pButton = ui_ButtonCreateImageOnly("pause_icon", ui_WidgetGetNextX(UI_WIDGET(pButton)) + 5, 5, skyEdPauseCB, pDoc);
	ui_ButtonSetImageStretch(pButton, true);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), SE_UI_PLAY_BUTTON_WIDTH, SE_UI_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	pButton = ui_ButtonCreateImageOnly("eui_button_plus", ui_WidgetGetNextX(UI_WIDGET(pButton)) + 5, 5, skyEdNewBlockCB, pDoc);
	ui_ButtonSetImageStretch(pButton, true);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), SE_UI_PLAY_BUTTON_WIDTH, SE_UI_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);


	ui_PaneAddChild(pDoc->UI.pTimelinePane, pTimeline);
}

static void skyEdDOFChangedCB(SkyBlockInfo *pInfo, MEField *pField, DOFValues *pDOF)
{
	pDOF->focusDist = MIN(pDOF->focusDist, pDOF->farDist);
	pDOF->nearDist = MIN(pDOF->nearDist, pDOF->focusDist);
}

static void skyEdFogChangedCB(SkyBlockInfo *pInfo, MEField *pField, SkyTimeFog *pFog)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;

	pFog->fogHeight[0] = MIN(pFog->fogHeight[0], pFog->fogHeight[1]);
	pFog->fogHeight[1] = MAX(pFog->fogHeight[0], pFog->fogHeight[1]);

	pFog->lowFogDist[0] = MIN(pFog->lowFogDist[0], pFog->lowFogDist[1]);
	pFog->lowFogDist[1] = MAX(pFog->lowFogDist[0], pFog->lowFogDist[1]);

	pFog->highFogDist[0] = MIN(pFog->highFogDist[0], pFog->highFogDist[1]);
	pFog->highFogDist[1] = MAX(pFog->highFogDist[0], pFog->highFogDist[1]);

	if(pField == pDoc->UI.pFogHeightLow) {
		pDoc->UI.iFogHeightFade = 255;
		pDoc->UI.fFogHeight = pFog->fogHeight[0];
	}

	if(pField == pDoc->UI.pFogHeightHigh) {
		pDoc->UI.iFogHeightFade = 255;
		pDoc->UI.fFogHeight = pFog->fogHeight[1];
	}
}

static void skyEdBlockValueChanged(MEField *pField, bool bFinished, SkyEditorDoc *pDoc)
{
	int i;

	if(pDoc->bRefreshingUI)
		return;

	if(!skyEdIsEditable(pDoc))
		return;

	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
		if(eaSize(&pInfo->ppSelected)==1 && pInfo->changedFunc)
			pInfo->changedFunc(pInfo, pField, pInfo->ppSelected[0]);
	}

	if(bFinished) {
		skyEdRefreshUI(pDoc);
		skyEdSetUnsaved(pDoc);
	} else {
		skyEdRefreshUI(pDoc);
	}
}

static void skyEdSkyDomeDisplayNameChanged(MEField *pField, bool bFinished, SkyEditorDoc *pDoc)
{
	int i;
	if(!pDoc->bRefreshingUI && bFinished) {
		SkyBlockInfo *pInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
		SkyDome *pDome = pInfo ? skyEdGetSkyDomeFromInfo(pInfo) : NULL;

		if(!skyEdIsEditable(pDoc))
			return;

		if(pDome) {
			const char *pcOldName = pInfo->pTrack->name;
			skyEdSetDomeDisplayNameUnique(pDoc, pDome, pDome->display_name);
			pInfo->pcDisplayName = allocAddString(pDome->display_name);
			//Fixup all UI string names
			for ( i=0; i < eaSize(&pDoc->UI.ppElementList); i++ ) {
				SkyEditorUIElement *pElem = pDoc->UI.ppElementList[i];
				if(stricmp_safe(pElem->pcBlockInfo, pcOldName)==0) {
					pElem->pcBlockInfo = pInfo->pcDisplayName;
				}
			}
			ui_TimelineTrackSetName(pInfo->pTrack, pDome->display_name);
		}
		skyEdRefreshUI(pDoc);
		skyEdSetUnsaved(pDoc);
	}
}

static void skyEdSkyDomeTypeChanged(UIComboBox *pCombo, int iNewType, SkyBlockInfo *pInfo)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;
	SkyDome *pSkyDome = skyEdGetSkyDomeFromInfo(pInfo);

	if(!skyEdIsEditable(pDoc))
		return;

	if(iNewType != SDT_StarField) {
		StructDestroySafe(parse_StarField, &pSkyDome->star_field);
	}
	if(iNewType != SDT_Atmosphere) {
		StructDestroySafe(parse_WorldAtmosphereProperties, &pSkyDome->atmosphere);
	}
	pSkyDome->luminary = (iNewType == SDT_Luminary);
	pSkyDome->luminary2 = (iNewType == SDT_Luminary2);

	if(iNewType == SDT_StarField && !pSkyDome->star_field) {
		pSkyDome->star_field = StructCreate(parse_StarField);
		pSkyDome->star_field->color_min[2] = 1.0f;
		pSkyDome->star_field->color_max[2] = 1.0f;
	}
	if(iNewType == SDT_Atmosphere && !pSkyDome->atmosphere) {
		pSkyDome->atmosphere = StructCreate(parse_WorldAtmosphereProperties);
	}

	StructFreeString(pSkyDome->name);
	switch(iNewType) {
	case SDT_Atmosphere:
		pSkyDome->name = StructAllocString("Atmosphere");
		break;
	case SDT_StarField:
		pSkyDome->name = StructAllocString("white");
		break;
	default:
		pSkyDome->name = StructAllocString("Sys_Unit_Quad");
		break;
	}

	eaDestroyStruct(&pSkyDome->tex_swaps, parse_MaterialNamedTexture);

	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
}

static void skyEdGetSkyDomeModelSwaps(SkyDome *pSkyDome, MaterialNamedTexture ***pppModelSwaps)
{
	//Find what swaps exist on the object
	if (pSkyDome->star_field) {
		materialGetMaterialNamedTextures(materialFind(pSkyDome->name, WL_FOR_UI), pppModelSwaps);
	} else {
		Model *pModel = NULL;
		if(pSkyDome->atmosphere)
			pModel = modelFind("_Sky_Atmosphere", false, WL_FOR_UI);
		else
			pModel = modelFind(pSkyDome->name, false, WL_FOR_UI);

		if(pModel)
		{
			MaterialTextureAssoc **ppMatTexAssocs = NULL;
			int j;

			wleGetModelTexMats(pModel, -1, &ppMatTexAssocs, false);

			for (j = 0; j < eaSize(&ppMatTexAssocs); j++)
				materialGetMaterialNamedTextures(materialFind(ppMatTexAssocs[j]->orig_name,WL_FOR_UI), pppModelSwaps);

			eaDestroyStruct(&ppMatTexAssocs, parse_MaterialTextureAssoc);
		}
	}
}

static bool skyEdSkyDomeTextureSwapCB(const char * const *ppTextureSwaps, const char * const *ppMaterialSwaps, SkyEditorDoc *pDoc)
{
	int i, j, k;
	SkyBlockInfo *pInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
	SkyDome *pSkyDome = pInfo ? skyEdGetSkyDomeFromInfo(pInfo) : NULL;
	MaterialNamedTexture **ppModelSwaps = NULL;

	if(!pSkyDome)
		return true;
	
	if(!skyEdIsEditable(pDoc))
		return true;

	skyEdGetSkyDomeModelSwaps(pSkyDome, &ppModelSwaps);

	for (i = 0; i < eaSize(&ppTextureSwaps); i+=2) {
		const char *pcSwapFrom = ppTextureSwaps[i];
		const char *pcSwapTo = ppTextureSwaps[i+1];

		//Find the matching swap and make sure we remove any existing swaps
		MaterialNamedTexture *pFoundModelSwap = NULL;
		for (j = 0; j < eaSize(&ppModelSwaps); j++) {
			MaterialNamedTexture *pModelSwap = ppModelSwaps[j];
			if (stricmp(pcSwapFrom, pModelSwap->texture_name) == 0) {
				for (k = 0; k < eaSize(&pSkyDome->tex_swaps); k++) {
					MaterialNamedTexture *actualSwap = pSkyDome->tex_swaps[k];
					if ((stricmp(actualSwap->op, pModelSwap->op) == 0) && (stricmp(actualSwap->input, pModelSwap->input) == 0)) {
						StructDestroy(parse_MaterialNamedTexture, eaRemove(&pSkyDome->tex_swaps, k));
						break;
					}
				}
				pFoundModelSwap = pModelSwap;
				break;
			}
		}
		//Add the new swap if one exists
		if (pcSwapTo && pcSwapTo[0]) {
			MaterialNamedTexture *pNewTextureSwap = StructCreate(parse_MaterialNamedTexture);

			assert(pFoundModelSwap);
			pNewTextureSwap->op = pFoundModelSwap->op;
			pNewTextureSwap->input = pFoundModelSwap->input;
			pNewTextureSwap->texture_name = pcSwapTo;
			pNewTextureSwap->texture = texFind(pNewTextureSwap->texture_name,true);
			eaPush(&pSkyDome->tex_swaps, pNewTextureSwap);
		}
	}

	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
	return true;
}

static void skyEdRefreshSkyDomeTextureSwaps(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyDome *pSkyDome, UIWidget *pParent, int *y_out)
{
	int y = (*y_out);
	MaterialNamedTexture **ppModelSwaps = NULL;
	static int iSwapsUISize;

	if(!pSkyDome)
		return;

	skyEdGetSkyDomeModelSwaps(pSkyDome, &ppModelSwaps);

	if(!pDoc->UI.pSkyDomeTextureSwapsUI) {
		int newYPpos;
		pDoc->UI.pSkyDomeTextureSwapsUI = calloc(1, sizeof(WleAESwapUI));
		newYPpos = wleAESwapsUICreate(pDoc->UI.pSkyDomeTextureSwapsUI, NULL, NULL, pParent, skyEdSkyDomeTextureSwapCB, pDoc, TEXTURE_SWAP, false, 5, y);
		iSwapsUISize = newYPpos-y;
	}
	y += iSwapsUISize;

	{
		int i, j;
		StashTable stashTextures = stashTableCreateWithStringKeys(16, StashDefault);

		for ( i=0; i < eaSize(&ppModelSwaps); i++ ) {
			MaterialNamedTexture *pModelSwap = ppModelSwaps[i];
			const char *pcSwapName = NULL;
			for ( j=0; j < eaSize(&pSkyDome->tex_swaps); j++ ) {
				MaterialNamedTexture *pDomeSwap = pSkyDome->tex_swaps[j];
				if ((strcmp(pDomeSwap->op, pModelSwap->op) == 0) && (strcmp(pDomeSwap->input, pModelSwap->input) == 0)) {
					pcSwapName = allocAddString(pDomeSwap->texture_name);
					break;
				}
			}
			stashAddPointer(stashTextures, pModelSwap->texture_name, pcSwapName, true);
		}

		wleAESwapsRebuildUI(pDoc->UI.pSkyDomeTextureSwapsUI, NULL, stashTextures);
		stashTableDestroy(stashTextures);
	}

	(*y_out) = y;
}

static void skyEdRefreshSkyDomeType(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyDome *pSkyDome, UIWidget *pParent, int y)
{
	if(!pDoc->UI.pSkyDomeTypeCombo) {
		pDoc->UI.pSkyDomeTypeCombo = ui_ComboBoxCreateWithEnum(EDITOR_CONTROL_X, y, 1, SkyDomeTypeEnum, skyEdSkyDomeTypeChanged, pInfo);
		skyEdSetUpWidget(UI_WIDGET(pDoc->UI.pSkyDomeTypeCombo), pParent);
	}
	ui_ComboBoxSetSelectedEnumCallback(pDoc->UI.pSkyDomeTypeCombo, skyEdSkyDomeTypeChanged, pInfo);

	if(pSkyDome->star_field) {
		ui_ComboBoxSetSelectedEnum(pDoc->UI.pSkyDomeTypeCombo, SDT_StarField);
	} else if (pSkyDome->atmosphere) {
		ui_ComboBoxSetSelectedEnum(pDoc->UI.pSkyDomeTypeCombo, SDT_Atmosphere);
	} else if (pSkyDome->luminary) {
		ui_ComboBoxSetSelectedEnum(pDoc->UI.pSkyDomeTypeCombo, SDT_Luminary);
	} else if (pSkyDome->luminary2) {
		ui_ComboBoxSetSelectedEnum(pDoc->UI.pSkyDomeTypeCombo, SDT_Luminary2);
	} else {
		ui_ComboBoxSetSelectedEnum(pDoc->UI.pSkyDomeTypeCombo, SDT_Object);
	}
}

static void skyEdUICCTypeComboChanged(void *pUnused, int i, SkyEditorDoc *pDoc)
{
	skyEdRefreshUI(pDoc);
}

static void skyEdCCCurvesRun(SkyEditorDoc *pDoc, SkyTimeColorCorrection *pValues, skyEdRunOnItemFunc run_func)
{
	int idx = ui_ComboBoxGetSelected(pDoc->UI.pCCCurvesChanelCombo);
	switch(idx) {
	case SCCC_Red:
		run_func(pDoc, idx, &pValues->curve_red);
		break;
	case SCCC_Green:
		run_func(pDoc, idx, &pValues->curve_green);
		break;
	case SCCC_Blue:
		run_func(pDoc, idx, &pValues->curve_blue);
		break;
	case SCCC_Intensity:
		run_func(pDoc, idx, &pValues->curve_intensity);
		break;
	case SCCC_Saturation:
		run_func(pDoc, idx, &pValues->saturation_curve);
		break;
	}	
}

static void skyEdCCCurvesGraphReset(SkyEditorDoc *pDoc, int idx, SkyColorCurve *pCurve)
{
	if(idx == SCCC_Saturation) {
		setVec2(pCurve->control_points[0], 0.00, 0.0);
		setVec2(pCurve->control_points[1], 0.33, 0.0);
		setVec2(pCurve->control_points[2], 0.66, 0.0);
		setVec2(pCurve->control_points[3], 1.00, 0.0);
		return;
	}
	setVec2same(pCurve->control_points[0], 0.00);
	setVec2same(pCurve->control_points[1], 0.33);
	setVec2same(pCurve->control_points[2], 0.66);
	setVec2same(pCurve->control_points[3], 1.00);
}

static void skyEdCCCurvesResetCB(UIButton *pButton, SkyBlockInfo *pInfo)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;
	SkyTimeColorCorrection *pValues;

	if(pDoc->bRefreshingUI)
		return;

	if(!skyEdIsEditable(pDoc))
		return;

	if(eaSize(&pInfo->ppSelected) != 1 || pInfo->pBlockPti != parse_SkyTimeColorCorrection)
		return;

	pValues = (SkyTimeColorCorrection*)pInfo->ppSelected[0];
	skyEdCCCurvesRun(pDoc, pValues, skyEdCCCurvesGraphReset);

	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);	
}

static void skyEdCCCurvesGraphChanged(SkyEditorDoc *pDoc, int idx, SkyColorCurve *pCurve)
{
	int i;
	for ( i=0; i < 4; i++ ) {
		UIGraphPoint *pPoint = ui_GraphGetPoint(pDoc->UI.pCCGraph, i);
		if(pPoint)
			copyVec2(pPoint->v2Position, pCurve->control_points[i]);
	}
}

static void skyEdCCCurvesChanged(SkyBlockInfo *pInfo, bool bFinished)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;
	SkyTimeColorCorrection *pValues;

	if(pDoc->bRefreshingUI)
		return;

	if(!skyEdIsEditable(pDoc))
		return;

	if(eaSize(&pInfo->ppSelected) != 1 || pInfo->pBlockPti != parse_SkyTimeColorCorrection)
		return;

	pValues = (SkyTimeColorCorrection*)pInfo->ppSelected[0];
	skyEdCCCurvesRun(pDoc, pValues, skyEdCCCurvesGraphChanged);

	if(bFinished) {
		skyEdRefreshUI(pDoc);
		skyEdSetUnsaved(pDoc);	
	} else {
		skyEdRefreshUI(pDoc);
	}
}

static void skyEdCCCurvesFinished(void *pUnused, SkyBlockInfo *pInfo)
{
	skyEdCCCurvesChanged(pInfo, true);
}

static void skyEdCCCurvesDragged(void *pUnused, SkyBlockInfo *pInfo)
{
	skyEdCCCurvesChanged(pInfo, false);
}

static void skyEdRefreshCCCurveGraph(SkyEditorDoc *pDoc, int idx, SkyColorCurve *pCurve)
{
	int i;
	ui_GraphSetLockToIndex(pDoc->UI.pCCGraph, false);
	for ( i=0; i < 4; i++ ) {
		ui_GraphMovePointIndex(pDoc->UI.pCCGraph, i, pCurve->control_points[i], 0);
	}
	ui_GraphSetLockToIndex(pDoc->UI.pCCGraph, true);
}

static int skyEdRefreshCCCurves(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeColorCorrection *pValues, UIWidget *pParent, int y)
{
	#define CCGraphSize 175
	UIGraph *pCCGraph = pDoc->UI.pCCGraph;
	UIComboBox *pCCCurvesChanelCombo = pDoc->UI.pCCCurvesChanelCombo;
	UIButton *pCCCurvesResetButton = pDoc->UI.pCCCurvesResetButton;
	UILabel *pLabel;

	////////////////
	// Ensure UI Exists

	if(!pCCCurvesChanelCombo) {

		pLabel = ui_LabelCreate("Channel", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));

		pCCCurvesChanelCombo = ui_ComboBoxCreateWithEnum(100, y, 1, SkyCCCurveTypesEnum, skyEdUICCTypeComboChanged, pDoc);
		ui_ComboBoxSetSelected(pCCCurvesChanelCombo, 0);
		skyEdSetUpWidget(UI_WIDGET(pCCCurvesChanelCombo), pParent);
		ui_WidgetSetPaddingEx(UI_WIDGET(pCCCurvesChanelCombo), 0, 55+EDITOR_RIGHT_PAD, 0, 0);
		pDoc->UI.pCCCurvesChanelCombo = pCCCurvesChanelCombo;

		pCCCurvesResetButton = ui_ButtonCreate("Reset", 5, y, skyEdCCCurvesResetCB, pInfo);
		ui_WidgetSetWidth(UI_WIDGET(pCCCurvesResetButton), 50);
		ui_WidgetSetPositionEx(UI_WIDGET(pCCCurvesResetButton), 5, y, 0, 0, UITopRight);
		ui_WidgetAddChild(pParent, UI_WIDGET(pCCCurvesResetButton));
		pDoc->UI.pCCCurvesResetButton = pCCCurvesResetButton;
	}
	y += EDITOR_ROW_HEIGHT;

	if(!pCCGraph) {
		Vec2 min = {0, 0};
		Vec2 max = {1, 1};
		pCCGraph = ui_GraphCreate("Input","Output", min, max, 4, true);
		ui_GraphSetChangedCallback(pCCGraph, skyEdCCCurvesFinished, pInfo);
		ui_GraphSetDraggingCallback(pCCGraph, skyEdCCCurvesDragged, pInfo);
		ui_GraphSetMinPoints(pCCGraph, 4);
		ui_GraphSetDrawScale(pCCGraph, true);
		ui_GraphSetDrawConnection(pCCGraph, true, false);
		ui_GraphSetLockToIndex(pCCGraph, true);
		ui_GraphSetResolution(pCCGraph, 4, 4);
		skyEdSetUpWidget(UI_WIDGET(pCCGraph), pParent);
		ui_WidgetSetHeight(UI_WIDGET(pCCGraph), CCGraphSize);
		ui_WidgetSetPosition(UI_WIDGET(pCCGraph), 5, y);
		pDoc->UI.pCCGraph = pCCGraph;
	}
	y += CCGraphSize;


	////////////////
	// Fill Data

	{
		int idx = ui_ComboBoxGetSelected(pCCCurvesChanelCombo);
		if(idx == SCCC_Saturation) {
			Vec2 min = {0, -1};
			Vec2 max = {1, 1};
			ui_GraphSetBounds(pCCGraph, min, max);
			ui_GraphSetLabels(pCCGraph, "Brightness", "Saturation");
		} else {
			Vec2 min = {0, 0};
			Vec2 max = {1, 1};
			ui_GraphSetBounds(pCCGraph, min, max);
			ui_GraphSetLabels(pCCGraph, "Input", "Output");
		}
	}

	skyEdCCCurvesRun(pDoc, pValues, skyEdRefreshCCCurveGraph);

	return y;
}

static void skyEdCCLevelSlidersReset(SkyEditorDoc *pDoc, int idx, SkyColorLevels *pLevels)
{
	pLevels->input_range[0] = 0.0f;
	pLevels->gamma = 1.0f;
	pLevels->input_range[1] = 1.0f;
	pLevels->output_range[0] = 0.0f;
	pLevels->output_range[1] = 1.0f;
}

static void skyEdCCLevelSlidersChanged(SkyEditorDoc *pDoc, int idx, SkyColorLevels *pLevels)
{
	UISlider *pCCLevelsInputSlider = pDoc->UI.pCCLevelsInputSlider; 
	UISlider *pCCLevelsOutputSlider = pDoc->UI.pCCLevelsOutputSlider; 
	F32 pre_input_start = pLevels->input_range[0];
	F32 pre_input_end = pLevels->input_range[1];
	F32 input_start = ui_SliderGetValueEx(pCCLevelsInputSlider, 0);
	F32 input_end = ui_SliderGetValueEx(pCCLevelsInputSlider, 2);
	F32 gamma = ui_SliderGetValueEx(pCCLevelsInputSlider, 1);

	input_start = CLAMP(input_start, 0, 0.9);
	input_end = CLAMP(input_end, input_start+0.01, 1);

	if(pre_input_end > pre_input_start) {
		gamma -= pre_input_start;
		gamma /= (pre_input_end - pre_input_start);
	}
	gamma = CLAMP(gamma, 0, 1);
	gamma = 1.0f - gamma;
	if(gamma > 0.5) {
		gamma = (gamma-0.5f)*2.0f*9.0f + 1.0f;
	} else {
		gamma *= 2;
	}
	gamma = CLAMP(gamma, 0.1, 9.99);

	pLevels->input_range[0] = input_start;
	pLevels->gamma = gamma;
	pLevels->input_range[1] = input_end;

	pLevels->output_range[0] = ui_SliderGetValueEx(pCCLevelsOutputSlider, 0);
	pLevels->output_range[1] = ui_SliderGetValueEx(pCCLevelsOutputSlider, 1);
}

static void skyEdCCLevelEntryChanged(SkyEditorDoc *pDoc, int idx, SkyColorLevels *pLevels)
{
	pLevels->input_range[0] =	atof(ui_TextEntryGetText(pDoc->UI.pCCLevelsTextEntries[0]))/255.0f;
	pLevels->gamma =			atof(ui_TextEntryGetText(pDoc->UI.pCCLevelsTextEntries[1]));
	pLevels->input_range[1] =	atof(ui_TextEntryGetText(pDoc->UI.pCCLevelsTextEntries[2]))/255.0f;
	pLevels->output_range[0] =	atof(ui_TextEntryGetText(pDoc->UI.pCCLevelsTextEntries[3]))/255.0f;
	pLevels->output_range[1] =	atof(ui_TextEntryGetText(pDoc->UI.pCCLevelsTextEntries[4]))/255.0f;
}

static void skyEdRefreshCCLevelSliders(SkyEditorDoc *pDoc, int idx, SkyColorLevels *pLevels)
{
	UISlider *pCCLevelsInputSlider = pDoc->UI.pCCLevelsInputSlider; 
	UISlider *pCCLevelsOutputSlider = pDoc->UI.pCCLevelsOutputSlider;
	F32 input_start = pLevels->input_range[0];
	F32 input_end = pLevels->input_range[1];
	F32 gamma = pLevels->gamma;
	
	if(gamma > 1) {
		gamma = 0.5f + ((gamma - 1.0f)/9.0f)/2.0f;
	} else {
		gamma /= 2;
	}
	gamma = 1.0f - gamma;
	if(input_end > input_start) {
		gamma *= (input_end - input_start);
		gamma += input_start;
	}

	ui_SliderSetValueAndCallbackEx(pCCLevelsInputSlider, 0, input_start, false, false);
	ui_SliderSetValueAndCallbackEx(pCCLevelsInputSlider, 1, gamma, false, false);
	ui_SliderSetValueAndCallbackEx(pCCLevelsInputSlider, 2, input_end, false, false);

	ui_SliderSetValueAndCallbackEx(pCCLevelsOutputSlider, 0, pLevels->output_range[0], false, false);
	ui_SliderSetValueAndCallbackEx(pCCLevelsOutputSlider, 1, pLevels->output_range[1], false, false);

	SE_SetTextFromFloat(pDoc->UI.pCCLevelsTextEntries[0], pLevels->input_range[0]*255.0f);
	SE_SetTextFromFloat(pDoc->UI.pCCLevelsTextEntries[1], pLevels->gamma);
	SE_SetTextFromFloat(pDoc->UI.pCCLevelsTextEntries[2], pLevels->input_range[1]*255.0f);
	SE_SetTextFromFloat(pDoc->UI.pCCLevelsTextEntries[3], pLevels->output_range[0]*255.0f);
	SE_SetTextFromFloat(pDoc->UI.pCCLevelsTextEntries[4], pLevels->output_range[1]*255.0f);
}

static void skyEdCCLevelsRun(SkyEditorDoc *pDoc, SkyTimeColorCorrection *pValues, skyEdRunOnItemFunc run_func)
{
	int idx = ui_ComboBoxGetSelected(pDoc->UI.pCCLevelsChanelCombo);
	switch(idx) {
	case SCCL_Red:
		run_func(pDoc, idx, &pValues->levels_red);
		break;
	case SCCL_Green:
		run_func(pDoc, idx, &pValues->levels_green);
		break;
	case SCCL_Blue:
		run_func(pDoc, idx, &pValues->levels_blue);
		break;
	case SCCL_Intensity:
		run_func(pDoc, idx, &pValues->levels_intensity);
		break;
	}	
}

static void skyEdCCLevelsResetCB(UIButton *pButton, SkyBlockInfo *pInfo)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;
	SkyTimeColorCorrection *pValues;

	if(pDoc->bRefreshingUI)
		return;

	if(!skyEdIsEditable(pDoc))
		return;

	if(eaSize(&pInfo->ppSelected) != 1 || pInfo->pBlockPti != parse_SkyTimeColorCorrection)
		return;

	pValues = (SkyTimeColorCorrection*)pInfo->ppSelected[0];
	skyEdCCLevelsRun(pDoc, pValues, skyEdCCLevelSlidersReset);

	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
}

static void skyEdCCLevelsChangedCB(void *pUnused, bool bFinished, SkyBlockInfo *pInfo)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;
	SkyTimeColorCorrection *pValues;

	if(pDoc->bRefreshingUI)
		return;

	if(!skyEdIsEditable(pDoc))
		return;

	if(eaSize(&pInfo->ppSelected) != 1 || pInfo->pBlockPti != parse_SkyTimeColorCorrection)
		return;

	pValues = (SkyTimeColorCorrection*)pInfo->ppSelected[0];
	skyEdCCLevelsRun(pDoc, pValues, skyEdCCLevelSlidersChanged);

	if(bFinished) {
		skyEdRefreshUI(pDoc);
		skyEdSetUnsaved(pDoc);
	} else {
		skyEdRefreshUI(pDoc);
	}
}

static void skyEdCCLevelsEntryChangedCB(void *pUnused, SkyBlockInfo *pInfo)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;
	SkyTimeColorCorrection *pValues;

	if(pDoc->bRefreshingUI)
		return;

	if(!skyEdIsEditable(pDoc))
		return;

	if(eaSize(&pInfo->ppSelected) != 1 || pInfo->pBlockPti != parse_SkyTimeColorCorrection)
		return;

	pValues = (SkyTimeColorCorrection*)pInfo->ppSelected[0];
	skyEdCCLevelsRun(pDoc, pValues, skyEdCCLevelEntryChanged);	

	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
}

static int skyEdRefreshCCLevels(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeColorCorrection *pValues, UIWidget *pParent, int y)
{
	UIComboBox *pCCLevelsChanelCombo = pDoc->UI.pCCLevelsChanelCombo;
	UIButton *pCCLevelsResetButton = pDoc->UI.pCCLevelsResetButton;
	UISlider *pCCLevelsInputSlider = pDoc->UI.pCCLevelsInputSlider; 
	UISlider *pCCLevelsOutputSlider = pDoc->UI.pCCLevelsOutputSlider; 
	UITextEntry *pEntry;
	UILabel *pLabel;

	////////////////
	// Ensure UI Exists

	if(!pCCLevelsChanelCombo) {

		pLabel = ui_LabelCreate("Channel", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));

		pCCLevelsChanelCombo = ui_ComboBoxCreateWithEnum(100, y, 1, SkyCCLevelTypesEnum, skyEdUICCTypeComboChanged, pDoc);
		ui_ComboBoxSetSelected(pCCLevelsChanelCombo, 0);
		skyEdSetUpWidget(UI_WIDGET(pCCLevelsChanelCombo), pParent);
		ui_WidgetSetPaddingEx(UI_WIDGET(pCCLevelsChanelCombo), 0, 55+EDITOR_RIGHT_PAD, 0, 0);
		pDoc->UI.pCCLevelsChanelCombo = pCCLevelsChanelCombo;

		pCCLevelsResetButton = ui_ButtonCreate("Reset", 5, y, skyEdCCLevelsResetCB, pInfo);
		ui_WidgetSetWidth(UI_WIDGET(pCCLevelsResetButton), 50);
		ui_WidgetSetPositionEx(UI_WIDGET(pCCLevelsResetButton), 5, y, 0, 0, UITopRight);
		ui_WidgetAddChild(pParent, UI_WIDGET(pCCLevelsResetButton));
		pDoc->UI.pCCLevelsResetButton = pCCLevelsResetButton;
	}
	y += EDITOR_ROW_HEIGHT;

	if(!pCCLevelsInputSlider) {
		pLabel = ui_LabelCreate("Input Levels", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));

		pCCLevelsInputSlider = ui_SliderCreate(5, y + EDITOR_ROW_HEIGHT, 1, 0, 1, 0);
		ui_SliderSetChangedCallback(pCCLevelsInputSlider, skyEdCCLevelsChangedCB, pInfo);
		ui_SliderSetPolicy(pCCLevelsInputSlider, UISliderContinuous);
		ui_SliderSetCount(pCCLevelsInputSlider, 3, 0.01);
		skyEdSetUpWidget(UI_WIDGET(pCCLevelsInputSlider), pParent);
		pDoc->UI.pCCLevelsInputSlider = pCCLevelsInputSlider;
	}
	y += EDITOR_ROW_HEIGHT*2;

	if(!pDoc->UI.pCCLevelsTextEntries[0]) {
		int i;
		for ( i=0; i < 5; i++ ) {
			pEntry = ui_TextEntryCreate("", 0, y);
			ui_WidgetSetWidth(UI_WIDGET(pEntry), 60);
			ui_TextEntrySetFinishedCallback(pEntry, skyEdCCLevelsEntryChangedCB, pInfo);
			ui_WidgetAddChild(pParent, UI_WIDGET(pEntry));
			pDoc->UI.pCCLevelsTextEntries[i] = pEntry;
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->UI.pCCLevelsTextEntries[0]), 5, y, 0, 0, UITopLeft); 
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->UI.pCCLevelsTextEntries[1]), 0, y, 0, 0, UITop); 
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->UI.pCCLevelsTextEntries[2]), 5, y, 0, 0, UITopRight); 
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->UI.pCCLevelsTextEntries[3]), 5, y+EDITOR_ROW_HEIGHT*3, 0, 0, UITopLeft); 
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->UI.pCCLevelsTextEntries[4]), 5, y+EDITOR_ROW_HEIGHT*3, 0, 0, UITopRight); 
	}
	y += EDITOR_ROW_HEIGHT;

	if(!pCCLevelsOutputSlider) {
		pLabel = ui_LabelCreate("Output Levels", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));

		pCCLevelsOutputSlider = ui_SliderCreate(5, y + EDITOR_ROW_HEIGHT, 1, 0, 1, 0);
		ui_SliderSetChangedCallback(pCCLevelsOutputSlider, skyEdCCLevelsChangedCB, pInfo);
		ui_SliderSetPolicy(pCCLevelsOutputSlider, UISliderContinuous);
		ui_SliderSetCount(pCCLevelsOutputSlider, 2, 0.01);
		skyEdSetUpWidget(UI_WIDGET(pCCLevelsOutputSlider), pParent);
		pDoc->UI.pCCLevelsOutputSlider = pCCLevelsOutputSlider;
	}
	y += EDITOR_ROW_HEIGHT*3;

	////////////////
	// Fill Data

	skyEdCCLevelsRun(pDoc, pValues, skyEdRefreshCCLevelSliders);	

	return y;
}

static void skyEdCCAdjustmentColorChangedCB(UIButton *pUnused, bool bFinished, SkyBlockInfo *pInfo) 
{
	int i;
	SkyEditorDoc *pDoc = pInfo->pDoc;
	SkyTimeColorCorrection *pValues;

	if(pDoc->bRefreshingUI)
		return;

	if(!skyEdIsEditable(pDoc))
		return;

	if(eaSize(&pInfo->ppSelected) != 1 || pInfo->pBlockPti != parse_SkyTimeColorCorrection)
		return;

	pValues = (SkyTimeColorCorrection*)pInfo->ppSelected[0];

	for ( i=0; i < 6; i++ ) {
		Vec4 rgb, hsv;
		UIColorButton *pButton = pDoc->UI.pCCAdjustmentButtons[i];

		ui_ColorButtonGetColor(pButton, rgb);
		rgbToHsv(rgb, hsv);

		pValues->specificHue[i] = (hsv[0] - i*60.0f) / 360.0f;
		pValues->specificSaturation[i] = hsv[1]-1.0f;
		pValues->specificValue[i] = hsv[2]-1.0f;
	}

	if(bFinished) {
		skyEdRefreshUI(pDoc);
		skyEdSetUnsaved(pDoc);
	} else {
		skyEdRefreshUI(pDoc);
	}
}

static int skyEdRefreshCCAdjustments(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeColorCorrection *pValues, UIWidget *pParent, int y)
{
	int i;
	UIColorButton *pButton;
	UILabel *pLabel;

	////////////////
	// Ensure UI Exists

	y += EDITOR_ROW_HEIGHT*0.5;
	if(!pDoc->UI.pCCAdjustmentButtons[0]) {
		for ( i=0; i < 6; i++ ) {
			pButton = ui_ColorButtonCreate(0,0,zerovec4);
			pButton->liveUpdate = true;
			pButton->noAlpha = true;
			ui_ColorButtonSetChangedCallback(pButton, skyEdCCAdjustmentColorChangedCB, pInfo);
			ui_WidgetAddChild(pParent, UI_WIDGET(pButton));
			pDoc->UI.pCCAdjustmentButtons[i] = pButton;
		}

		// Red
		pButton = pDoc->UI.pCCAdjustmentButtons[0];
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITop);
		pLabel = ui_LabelCreate("R", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 0, y+EDITOR_ROW_HEIGHT, 0, 0, UITop);

		// Yellow
		pButton = pDoc->UI.pCCAdjustmentButtons[1];
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 150, y+EDITOR_ROW_HEIGHT*1.5, 0, 0, UITop);
		pLabel = ui_LabelCreate("Y", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 50, y+EDITOR_ROW_HEIGHT*1.5, 0, 0, UITop);

		// Magenta
		pButton = pDoc->UI.pCCAdjustmentButtons[5];
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -150, y+EDITOR_ROW_HEIGHT*1.5, 0, 0, UITop);
		pLabel = ui_LabelCreate("M", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), -50, y+EDITOR_ROW_HEIGHT*1.5, 0, 0, UITop);

		// Green
		pButton = pDoc->UI.pCCAdjustmentButtons[2];
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 150, y+EDITOR_ROW_HEIGHT*3, 0, 0, UITop);
		pLabel = ui_LabelCreate("G", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 50, y+EDITOR_ROW_HEIGHT*3, 0, 0, UITop);

		// Blue
		pButton = pDoc->UI.pCCAdjustmentButtons[4];
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -150, y+EDITOR_ROW_HEIGHT*3, 0, 0, UITop);
		pLabel = ui_LabelCreate("B", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), -50, y+EDITOR_ROW_HEIGHT*3, 0, 0, UITop);

		// Cyan
		pButton = pDoc->UI.pCCAdjustmentButtons[3];
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y+EDITOR_ROW_HEIGHT*4.5, 0, 0, UITop);
		pLabel = ui_LabelCreate("C", EDITOR_LEFT_PAD, y); 
		ui_WidgetAddChild(pParent, UI_WIDGET(pLabel));
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 0, y+EDITOR_ROW_HEIGHT*3.5, 0, 0, UITop);
	}
	y += EDITOR_ROW_HEIGHT*6;

	////////////////
	// Fill Data
	for ( i=0; i < 6; i++ ) {
		Vec4 rgb, hsv;
		pButton = pDoc->UI.pCCAdjustmentButtons[i];

		hsv[0] = (pValues->specificHue[i] * 360) + i*60.0f;
		hsv[1] = pValues->specificSaturation[i] + 1.0f;
		hsv[2] = pValues->specificValue[i] + 1.0f;
		hsv[3] = 0.0f; 
		hsvToRgb(hsv, rgb);
		ui_ColorButtonSetColor(pButton, rgb);
	}

	return y;
}

static SkyEditorUIElement* skyEdGetElementUI(SkyEditorDoc *pDoc, const char *pcBlockInfo, const char *pcFieldName, const char *pcDisplayName)
{
	int i;
	bool bFound = false;
	SkyEditorUIElement *pElem;
	for ( i=0; i < eaSize(&pDoc->UI.ppElementList); i++ ) {
		 pElem = pDoc->UI.ppElementList[i];
		if(pElem->pcBlockInfo == pcBlockInfo && pElem->pcFieldName == pcFieldName && pElem->pcDisplayName == pcDisplayName) {
			return pElem;
		}
	}
	pElem = calloc(1, sizeof(SkyEditorUIElement));
	pElem->pcBlockInfo = pcBlockInfo;
	pElem->pcFieldName = pcFieldName;
	pElem->pcDisplayName = pcDisplayName;
	eaPush(&pDoc->UI.ppElementList, pElem);
	return pElem;
}

static bool skyEdSkyDomeObjectPickerCB(EMPicker *pPicker, EMPickerSelection **ppSelections, SkyEditorDoc *pDoc)
{
	SkyBlockInfo *pInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
	SkyDome *pDome = pInfo ? skyEdGetSkyDomeFromInfo(pInfo) : NULL;
	ResourceInfo *pEntry;
	GroupDef *pDef;

	if(!skyEdIsEditable(pDoc))
		return false;

	if(!pDome || eaSize(&ppSelections) != 1 || ppSelections[0]->table != parse_ResourceInfo) 
		return false;

	pEntry = ppSelections[0]->data;
	
	pDef = objectLibraryGetGroupDefByName(pEntry->resourceName, false);
	if(!pDef) {
		Alertf("Can not find groupd def: %s", pEntry->resourceName);
		return false;
	}
	if(!modelFind(pDef->model_name, false, WL_FOR_UI)) {
		Alertf("Can not find model: %s", pDef->model_name);
		return false;
	}

	StructFreeStringSafe(&pDome->name);
	pDome->name = StructAllocString(pDef->model_name);

	eaDestroyStruct(&pDome->tex_swaps, parse_MaterialNamedTexture);

	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
	return true;
}

static void skyEdSkyDomeObjectButtonCB(UIButton *pButton, SkyEditorDoc *pDoc)
{
	EMPicker* pPicker = emPickerGetByName("Object Picker");
	assert(pPicker);
	emPickerShow(pPicker, "Select", false, skyEdSkyDomeObjectPickerCB, pDoc);
}

static void skyEdRefreshSkyDomeObject(SkyEditorDoc *pDoc, const char *pcGroupName, const char *pcValueName, UIWidget *pParent, int *y_out)
{
	int y = (*y_out);
	SkyEditorUIElement *pElem = skyEdGetElementUI(pDoc, pcGroupName, "Geometry", NULL);
	UIButton *pButton = pElem->pWidget;

	MEExpanderRefreshLabel(&pElem->pLabel, "Geometry", "Name of the geometry to draw.", EDITOR_LEFT_PAD, 0, y, pParent);

	if(!pButton) {
		pButton = ui_ButtonCreate("", EDITOR_CONTROL_X, y, skyEdSkyDomeObjectButtonCB, pDoc);
		pElem->pWidget = pButton;
		skyEdSetUpWidget(pElem->pWidget, pParent);
	}
	ui_ButtonSetText(pButton, pcValueName);

	y += EDITOR_ROW_HEIGHT;

	(*y_out) = y;
}

static void skyEdFlareListNameCB(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrPrintf(output, "Lens Flare %d", row);
}

static void skyEdFlareListSelectionChangedCB(UIList *pList, SkyEditorDoc *pDoc)
{
	SkyBlockInfo *pInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
	int newIdx = ui_ListGetSelectedRow(pList);
	if(pInfo) {
		pInfo->iSelectedLensFlare = newIdx;
	}
	skyEdRefreshUI(pDoc);
}

static void skyEdFlareNewButtonCB(UIButton *pButton, SkyEditorDoc *pDoc)
{
	SkyBlockInfo *pInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
	SkyDome *pDome = pInfo ? skyEdGetSkyDomeFromInfo(pInfo) : NULL;
	LensFlarePiece *pPiece = StructCreate(parse_LensFlarePiece);

	if(!skyEdIsEditable(pDoc))
		return;

	if(!pDome)
		return;

	if(!pDome->lens_flare)
		pDome->lens_flare = StructCreate(parse_LensFlare);
	
	pInfo->iSelectedLensFlare = eaPush(&pDome->lens_flare->flares, pPiece);
	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
}

static void skyEdFlareDeleteButtonCB(UIButton *pButton, SkyEditorDoc *pDoc)
{
	SkyBlockInfo *pInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
	SkyDome *pDome = pInfo ? skyEdGetSkyDomeFromInfo(pInfo) : NULL;
	LensFlarePiece *pPiece;

	if(!skyEdIsEditable(pDoc))
		return;

	if(!pDome)
		return;

	if(!pDome->lens_flare)
		return;

	if(pInfo->iSelectedLensFlare < 0 || pInfo->iSelectedLensFlare >= eaSize(&pDome->lens_flare->flares))
		return;

	pPiece = eaRemove(&pDome->lens_flare->flares, pInfo->iSelectedLensFlare);
	StructDestroy(parse_LensFlarePiece, pPiece);

	if(eaSize(&pDome->lens_flare->flares) == 0) {
		StructDestroySafe(parse_LensFlare, &pDome->lens_flare);
	}

	pInfo->iSelectedLensFlare = -1;
	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
}

static void skyEdRefreshSkyDomeFlares(SkyEditorDoc *pDoc, const char *pcGroupName, SkyBlockInfo *pInfo, SkyDome *pDome, UIWidget *pParent, int *y_out)
{
	int y = (*y_out);
	SkyEditorUIElement *pListElem = skyEdGetElementUI(pDoc, pcGroupName, "FlareList", NULL);
	SkyEditorUIElement *pNewButtonElem = skyEdGetElementUI(pDoc, pcGroupName, "NewFlareButton", NULL);
	SkyEditorUIElement *pDeleteButtonElem = skyEdGetElementUI(pDoc, pcGroupName, "DeleteFlareButton", NULL);
	UIList *pList = pListElem->pWidget;
	UIButton *pNewButton = pNewButtonElem->pWidget;
	UIButton *pDeleteButton = pDeleteButtonElem->pWidget;
	static void **ppDummyList = NULL;

	if(!pList) {
		UIListColumn *pCol;
		pList = ui_ListCreate(NULL, &ppDummyList, 15);
		pListElem->pWidget = pList;
		ui_ListSetSelectedCallback(pList, skyEdFlareListSelectionChangedCB, pDoc);
		ui_WidgetSetPosition(pListElem->pWidget, 5, y);
		ui_WidgetSetHeight(pListElem->pWidget, 90);
		skyEdSetUpWidget(pListElem->pWidget, pParent);

		pCol = ui_ListColumnCreate(UIListTextCallback, "Lens Flare", (intptr_t) skyEdFlareListNameCB, NULL);
		ui_ListColumnSetWidth(pCol, true, 1);
		ui_ListAppendColumn(pList, pCol);
	}
	y += 90;

	if(pDome->lens_flare) {
		eaSetSize(&ppDummyList, eaSize(&pDome->lens_flare->flares));
		ui_ListSetSelectedRow(pList, pInfo->iSelectedLensFlare); 
	} else {
		eaSetSize(&ppDummyList, 0);
	}

	if(!pNewButton) {
		pNewButton = ui_ButtonCreate("New", 5, y, skyEdFlareNewButtonCB, pDoc);
		pNewButtonElem->pWidget = pNewButton;
		ui_WidgetSetWidthEx(pNewButtonElem->pWidget, 0.49, UIUnitPercentage);
		ui_WidgetAddChild(pParent, pNewButtonElem->pWidget);
	}

	if(!pDeleteButton) {
		pDeleteButton = ui_ButtonCreate("Delete", 5, y, skyEdFlareDeleteButtonCB, pDoc);
		pDeleteButtonElem->pWidget = pDeleteButton;
		ui_WidgetSetPositionEx(pDeleteButtonElem->pWidget, 5, y, 0, 0, UITopRight);
		ui_WidgetSetWidthEx(pDeleteButtonElem->pWidget, 0.49, UIUnitPercentage);
		ui_WidgetAddChild(pParent, pDeleteButtonElem->pWidget);
	}
	y += EDITOR_ROW_HEIGHT;

	(*y_out) = y;
}

static UILabel* skyEdRefreshUILabel(SkyEditorDoc *pDoc, const char *pcGroupName, const char *pcDisplayName, const char *pcToolTip, UIWidget *pParent, int *y_out)
{
	int y = (*y_out);
	SkyEditorUIElement *pElem = skyEdGetElementUI(pDoc, pcGroupName, NULL, pcDisplayName);
	UILabel **ppLabel = &pElem->pLabel;

	MEExpanderRefreshLabel(ppLabel, pcDisplayName, pcToolTip, EDITOR_LEFT_PAD, 0, y, pParent);
	y += EDITOR_ROW_HEIGHT;
	(*y_out) = y;
	return (*ppLabel);
}

static void skyEdCopySkyBlock(UIMenuItem *pItem, skyEdCopyBlock *pSkyBlock)
{
	if (!skyEdClipBoard) {
		skyEdClipBoard = calloc(1, sizeof(skyEdCopyBlock));
	}
	if (skyEdClipBoard->pSkyBlock)
		StructDestroyVoid( (skyEdClipBoard->skyType) , (skyEdClipBoard->pSkyBlock) );
	skyEdClipBoard->skyType = pSkyBlock->skyType;
	skyEdClipBoard->pSkyBlock = StructCreateVoid(skyEdClipBoard->skyType);
	StructCopyAllVoid(pSkyBlock->skyType, pSkyBlock->pSkyBlock, skyEdClipBoard->pSkyBlock);
}

static void skyEdPasteSkyBlock(UIMenuItem *pItem, skyEdCopyBlock *pSkyPasteBlock)
{
	float time;
	if (!skyEdClipBoard) {
		return;			// nothing in the clipboard
	}
	if (pSkyPasteBlock->skyType != skyEdClipBoard->skyType) {
		return;			// skyTypes don't match, so what's in the clipboard should not be copied.
	}

	assert(pSkyPasteBlock->pDoc);

	if(!skyEdIsEditable(pSkyPasteBlock->pDoc))
		return;

	// The references to time may seem strange but this is sane since the first value of a skyinfoblock is always time.
	time = *(float*)(pSkyPasteBlock->pSkyBlock);
	StructCopyAllVoid(pSkyPasteBlock->skyType, skyEdClipBoard->pSkyBlock, pSkyPasteBlock->pSkyBlock);
	*(float*)(pSkyPasteBlock->pSkyBlock) = time;
	skyEdRefreshUI(pSkyPasteBlock->pDoc);
	skyEdSetUnsaved(pSkyPasteBlock->pDoc);
}

static void skyEdUIMenuPreopenCallback(UIMenu *pMenu, SkyEditorDoc *pDoc)
{
	return;
}

// Only one of these should be visible at a time.

static UIMenuButton *pCopyPasteSkyBlockMenuButton = NULL;

static void skyEdRemoveCopyPasteSkyBlockMenuButton()
{
	if (pCopyPasteSkyBlockMenuButton && UI_WIDGET(pCopyPasteSkyBlockMenuButton)->group)
		ui_WidgetGroupRemove(UI_WIDGET(pCopyPasteSkyBlockMenuButton)->group, UI_WIDGET(pCopyPasteSkyBlockMenuButton));
}

static void skyEdRefreshCopyPasteEX( SkyEditorDoc *pDoc, const char *pcGroupName, ParseTable *pParseTable, void *pSkyBlock, UIWidget *pParent, int *y_out )
{
	static skyEdCopyBlock *skyEdCopyPasteBlock = NULL;
	static bool firstTime = true;
	int y = *y_out;

	if (!skyEdCopyPasteBlock) {
		skyEdCopyPasteBlock = calloc(1,sizeof(skyEdCopyBlock));
	}
	skyEdCopyPasteBlock->skyType = pParseTable;

	if (firstTime)
		pCopyPasteSkyBlockMenuButton = ui_MenuButtonCreate(2, y+2);

	skyEdCopyPasteBlock->pSkyBlock = pSkyBlock;
	skyEdCopyPasteBlock->pDoc = pDoc;

	// remove from previous group so can be added to current one.
	skyEdRemoveCopyPasteSkyBlockMenuButton();

	ui_WidgetSetPositionEx(UI_WIDGET(pCopyPasteSkyBlockMenuButton), 2, y+2, 0, 0, UITopLeft);

	if (firstTime) {
		ui_MenuButtonAppendItems(pCopyPasteSkyBlockMenuButton,
			ui_MenuItemCreate("Copy Sky Block", UIMenuCallback, skyEdCopySkyBlock, skyEdCopyPasteBlock, NULL),
			ui_MenuItemCreate("Paste Sky Block", UIMenuCallback, skyEdPasteSkyBlock, skyEdCopyPasteBlock, NULL),
			NULL);
		ui_MenuButtonSetPreopenCallback(pCopyPasteSkyBlockMenuButton, skyEdUIMenuPreopenCallback, pDoc);
		firstTime = false;
	}

	ui_WidgetAddChild(pParent, UI_WIDGET(pCopyPasteSkyBlockMenuButton));

	y += 25;
	(*y_out) = y;
}

static bool skyEdMEFieldPreChangeCB(MEField *pField, bool bFinished, SkyEditorDoc *pDoc)
{
	if(pDoc->bRefreshingUI)
		return true;

	return skyEdIsEditable(pDoc);
}

static void skyEdMEFieldChangeCB(MEField *pField, bool bFinished, SkyEditorDoc *pDoc)
{
	if(pDoc->bRefreshingUI)
		return;

	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);
}

static MEField* skyEdRefreshUIElemEX(	SkyEditorDoc *pDoc, const char *pcGroupName, ParseTable *pParseTable, MEFieldType eFieldType, const char *pcFieldName,
										const char *pcDisplayName, const char *pcToolTip, void *pOrig, void *pData, UIWidget *pParent, 
										F32 min, F32 max, F32 step, int arrayIdx, MEFieldChangeCallback change_func, MEFieldColorType eColorType, int *y_out)
{
	int y = (*y_out);
	bool pFieldCreated;
	SkyEditorUIElement *pElem = skyEdGetElementUI(pDoc, pcGroupName, pcFieldName, pcDisplayName);
	UILabel **ppLabel = &pElem->pLabel;
	MEField **ppField = &pElem->pField;

	pFieldCreated = !(*ppField);

	MEExpanderRefreshLabel(ppLabel, pcDisplayName, pcToolTip, EDITOR_LEFT_PAD, 0, y, pParent);

	switch (eFieldType) {
		case kMEFieldType_Color:
			MEExpanderRefreshColorField(ppField, pOrig, pData, pParseTable, pcFieldName, eFieldType, pParent, EDITOR_CONTROL_X, y, 0, 1, 
										UIUnitPercentage, EDITOR_RIGHT_PAD, change_func, skyEdMEFieldPreChangeCB, pDoc, eColorType);
			(*ppField)->pUIColor->max = 5.0;
			(*ppField)->pUIColor->noAlpha = 1;
			break;
		case kMEFieldType_SliderText:
			MEExpanderRefreshTextSliderField(ppField, pOrig, pData, pParseTable, pcFieldName, eFieldType, pParent, EDITOR_CONTROL_X, y, 0, 1, 
										UIUnitPercentage, EDITOR_RIGHT_PAD, min, max, step, arrayIdx, change_func, skyEdMEFieldPreChangeCB, pDoc);
			break;
		case kMEFieldType_Texture:
			MEExpanderRefreshSimpleField(ppField, pOrig, pData, pParseTable, pcFieldName, eFieldType, pParent, EDITOR_CONTROL_X, y, 0, 1, 
										UIUnitPercentage, EDITOR_RIGHT_PAD, change_func, skyEdMEFieldPreChangeCB, pDoc);
			ui_WidgetSetHeight((*ppField)->pUIWidget, EDITOR_ROW_HEIGHT-3);
		default:
			MEExpanderRefreshSimpleField(ppField, pOrig, pData, pParseTable, pcFieldName, eFieldType, pParent, EDITOR_CONTROL_X, y, 0, 1, 
										UIUnitPercentage, EDITOR_RIGHT_PAD, change_func, skyEdMEFieldPreChangeCB, pDoc);
	}

	y += EDITOR_ROW_HEIGHT;

	(*y_out) = y;

	return (*ppField);
}

#define skyEdRefreshUILbl(displayName, toolTip)	skyEdRefreshUILabel(pDoc, pcGroupName, (displayName), (toolTip), pParent, &y)
#define skyEdRefreshUIFlt(fieldName, displayName, toolTip, min, max, step)					skyEdRefreshUIElemEX(pDoc, pcGroupName, pFuncPrsTbl, kMEFieldType_SliderText,	(fieldName), (displayName), (toolTip), pOrigSelected, pSelected, pParent, min, max, step,     -1, skyEdBlockValueChanged, 0, &y)
#define skyEdRefreshUIFIdx(fieldName, displayName, toolTip, min, max, step, aryIdx)			skyEdRefreshUIElemEX(pDoc, pcGroupName, pFuncPrsTbl, kMEFieldType_SliderText,	(fieldName), (displayName), (toolTip), pOrigSelected, pSelected, pParent, min, max, step, aryIdx, skyEdBlockValueChanged, 0, &y)
#define skyEdRefreshUIHSV(fieldName, displayName, toolTip)									skyEdRefreshUIElemEX(pDoc, pcGroupName, pFuncPrsTbl, kMEFieldType_Color,		(fieldName), (displayName), (toolTip), pOrigSelected, pSelected, pParent,   0,   0,    0,     -1, skyEdBlockValueChanged, kMEFieldColorType_HSV, &y)
#define skyEdRefreshUIRGB(fieldName, displayName, toolTip)									skyEdRefreshUIElemEX(pDoc, pcGroupName, pFuncPrsTbl, kMEFieldType_Color,		(fieldName), (displayName), (toolTip), pOrigSelected, pSelected, pParent,   0,   0,    0,     -1, skyEdBlockValueChanged, kMEFieldColorType_RGB, &y)
#define skyEdRefreshUIAny(fieldType, fieldName, displayName, toolTip)						skyEdRefreshUIElemEX(pDoc, pcGroupName, pFuncPrsTbl, (fieldType),				(fieldName), (displayName), (toolTip), pOrigSelected, pSelected, pParent,   0,   0,    0,     -1, skyEdBlockValueChanged, 0, &y)
#define skyEdRefreshUIAnyWithFunc(fieldType, function, fieldName, displayName, toolTip)		skyEdRefreshUIElemEX(pDoc, pcGroupName, pFuncPrsTbl, (fieldType),				(fieldName), (displayName), (toolTip), pOrigSelected, pSelected, pParent,   0,   0,    0,     -1, function, 0, &y)


#define skyEdRefreshUITime() skyEdRefreshUIElemEX(pDoc, pcGroupName, pFuncPrsTbl, kMEFieldType_SliderText, "Time", "Time", "Time of day (0 - 24) at which this SkyTime applies", pOrigSelected, pSelected, pParent, 0, 24, 0.1,     -1, skyEdBlockValueChanged, 0, &y)
#define skyEdRefreshUICopyPaste() skyEdRefreshCopyPasteEX(pDoc, pcGroupName, pFuncPrsTbl, pSelected, pParent, &y)
#define skyEdRefreshFuncInit(parseTable)	const char *pcGroupName = pInfo->pcDisplayName;	\
											ParseTable *pFuncPrsTbl = parseTable; \
											skyEdRefreshUICopyPaste(); \
											skyEdRefreshUITime(); \
											y += EDITOR_ROW_HALF_HEIGHT;


static int skyEdRefreshSkyDomeLuminaryPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyDome *pOrigSelected, SkyDome *pSelected, UIWidget *pParent)
{
	int y = 5;
	const char *pcGroupName = "SkyDomeLuminary";
	ParseTable *pFuncPrsTbl = parse_SkyDome;

	skyEdRefreshSkyDomeObject(pDoc, pcGroupName, pSelected->name, pParent, &y);
	
	skyEdRefreshSkyDomeFlares(pDoc, pcGroupName, pInfo, pSelected, pParent, &y);

	return y;
}

static int skyEdRefreshSkyDomeLensFlarePanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, LensFlarePiece *pOrigSelected, LensFlarePiece *pSelected, UIWidget *pParent)
{
	int y = 5;
	const char *pcGroupName = "SkyDomeLensFlare";
	ParseTable *pFuncPrsTbl = parse_LensFlarePiece;

	skyEdRefreshUIAny(kMEFieldType_TextEntry, "MaterialName",		"Material Name",	"Name of material for the flare");
	skyEdRefreshUIAny(kMEFieldType_TextEntry, "TextureName",		"Texture Name",		"Name of texture for the flare");

	skyEdRefreshUIFlt("Size",	"Size",		"Screen size scale of the flare piece. 1 indicates the width of the screen, 0 indicates no extent and intermediate values scale between these values.", 0, 1, 0.01);
	skyEdRefreshUIHSV("Color",	"Color",	"Color tint.");
	skyEdRefreshUIFlt("Offset",	"Offset",	"Relative position along the line between the light source on screen and the center of the screen. 0 indicates at the light source, 1 indicates the point opposite the light source across the center of the screen, and intermediate values indicate positions along the line. Values outside [0,1] indicate values past either end of the line.", -3, 3, 0.01);

	return y;
}

static int skyEdRefreshSkyDomeObjectPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyDome *pOrigSelected, SkyDome *pSelected, UIWidget *pParent)
{
	int y = 5;
	const char *pcGroupName = "SkyDomeObject";
	ParseTable *pFuncPrsTbl = parse_SkyDome;

	skyEdRefreshSkyDomeObject(pDoc, pcGroupName, pSelected->name, pParent, &y);
	skyEdRefreshUIFlt("NumberOfLoops",					"Number of Loops",		"How many times will this object travel through the sky per day.", 0, 1000, 1);
	skyEdRefreshUIFlt("LoopFadePercent",				"Loop Fade Percent",	"Percent of the loop at the begining and end for which to fade in and out.", 0, 1, 0.001);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIFIdx("StartPos",	"Start Pos X",	"Start postion for each loop.", -30000, 30000, 1, 0);
	skyEdRefreshUIFIdx("StartPos",	"Start Pos Y",	"Start postion for each loop.", -30000, 30000, 1, 1);
	skyEdRefreshUIFIdx("StartPos",	"Start Pos Z",	"Start postion for each loop.", -30000, 30000, 1, 2);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIFIdx("EndPos",	"End Pos X",	"End Positon for each loop.", -30000, 30000, 1, 0);
	skyEdRefreshUIFIdx("EndPos",	"End Pos Y",	"End Positon for each loop.", -30000, 30000, 1, 1);
	skyEdRefreshUIFIdx("EndPos",	"End Pos Z",	"End Positon for each loop.", -30000, 30000, 1, 2);

	return y;
}

static int skyEdRefreshSkyDomeStarFieldPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, StarField *pOrigSelected, StarField *pSelected, UIWidget *pParent)
{
	int y = 5;
	SkyDome *pSkyDome = skyEdGetSkyDomeFromInfoEx(pInfo, pDoc->pSkyInfo);
	SkyDome *pOrigSkyDome = skyEdGetSkyDomeFromInfoEx(pInfo, pDoc->pOrigSkyInfo);
	const char *pcGroupName = "SkyDomeStarField";
	ParseTable *pFuncPrsTbl = parse_StarField;

	skyEdRefreshUIElemEX(pDoc, pcGroupName, parse_SkyDome, kMEFieldType_TextEntry, 
						"Name", "Material Name", "Name of the material to use for the stars.",
						pOrigSkyDome, pSkyDome, pParent, 0, 0, 0, -1, skyEdBlockValueChanged, 0, &y);

	skyEdRefreshUIAny(kMEFieldType_TextEntry, "StarSeed",		"Seed",					"The random seed for this star field.");
	skyEdRefreshUIFlt("StarCount",								"Count",				"The number of stars in this star field.", 1, 10000, 1);
	skyEdRefreshUIFlt("StarSizeMin",							"Size Min",				"The min size of stars, in percent of screen space (at fov 55 or current fov, depending on the value of ScaleWithFov).", 0, 1000, 0.01);
	skyEdRefreshUIFlt("StarSizeMax",							"Size Max",				"The max size of stars, in percent of screen space (at fov 55 or current fov, depending on the value of ScaleWithFov).", 0, 1000, 0.01);
	skyEdRefreshUIHSV("StarColorHSVMin",						"HSV Min",				"The min color of stars in HSV space.");
	skyEdRefreshUIHSV("StarColorHSVMax",						"HSV Max",				"The max color of stars in HSV space.");
	skyEdRefreshUIAny(kMEFieldType_Check, "UseRandomRotation",	"Random Rotation",	"Randomly rotate the star sprites.");
	skyEdRefreshUIAny(kMEFieldType_Check, "ScaleWithFov",		"Scale With FOV",		"Star size should be interpreted as percent of screen space.");

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIFIdx("SliceAxis",								"Slice Axis X",			"Axis around which to place stars.", -1, 1, 0.001, 0);
	skyEdRefreshUIFIdx("SliceAxis",								"Slice Axis Y",			"Axis around which to place stars.", -1, 1, 0.001, 1);
	skyEdRefreshUIFIdx("SliceAxis",								"Slice Axis Z",			"Axis around which to place stars.", -1, 1, 0.001, 2);
	skyEdRefreshUIFlt("SliceAngle",								"Slice Angle",			"Angle for the slice to place stars in (in degrees, 0-90).", 0, 360, 0.1);
	skyEdRefreshUIFlt("SliceFade",								"Slice Fade",			"Power to fade the stars out at the edges of the slice, 0 to disable fading.", 0, 10, 0.1);
	skyEdRefreshUIAny(kMEFieldType_Check, "HalfDome",			"Half Dome",			"Only put stars in the top half of the sky.");

	return y;
}

static int skyEdRefreshSkyDomeAtmospherePanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, WorldAtmosphereProperties *pOrigSelected, WorldAtmosphereProperties *pSelected, UIWidget *pParent)
{
	int y = 5;
	SkyDome *pSkyDome = skyEdGetSkyDomeFromInfoEx(pInfo, pDoc->pSkyInfo);
	SkyDome *pOrigSkyDome = skyEdGetSkyDomeFromInfoEx(pInfo, pDoc->pOrigSkyInfo);
	const char *pcGroupName = "SkyDomeAtmosphere";
	ParseTable *pFuncPrsTbl = parse_WorldAtmosphereProperties;

	skyEdRefreshUIElemEX(pDoc, pcGroupName, parse_SkyDome, kMEFieldType_Color, 
		"AtmosphereSunHSV", "Sun HSV", "Diffuse lighting value for the sun used to light the atmosphere.",
		pOrigSkyDome, pSkyDome, pParent, 0, 0, 0, -1, skyEdBlockValueChanged, 0, &y);

	skyEdRefreshUIFlt("PlanetRadius",			"Planet Radius",	"Radius of the planet", 0, 100, 0.1);
	skyEdRefreshUIFlt("AtmosphereThickness",	"Thickness",		"Thickness of the amosphere.", 0, 10, 0.01);

	return y;
}

static void skyEdSkyDomeRotationAxisButtonEB(UIButton *pButton, SkyEditorDoc *pDoc)
{
	SkyBlockInfo *pSelectedInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
	SkyDome *pSkyDome = pSelectedInfo ? skyEdGetSkyDomeFromInfo(pSelectedInfo) : NULL;
	if(pSkyDome) {
		GfxCameraController *pCamera = gfxGetActiveCameraController();
		Mat3 camMat;

		if(!skyEdIsEditable(pDoc))
			return;

		createMat3YPR(camMat, pCamera->campyr);

		copyVec3(camMat[0], pSkyDome->rotation_axis);
		if(eaSize(&pSkyDome->dome_values) == 1) {
			SkyDomeTime *pTime = pSkyDome->dome_values[0];
			pTime->angle = DEG(pCamera->campyr[0]) + 270.0f;
			while(pTime->angle > 360.0f) pTime->angle -= 360.0f;
			while(pTime->angle < -360.0f) pTime->angle += 360.0f;
		}

		skyEdRefreshUI(pDoc);
		skyEdSetUnsaved(pDoc);	
	}
}

static void skyEdRefreshSkyDomeRotationAxisButton(SkyEditorDoc *pDoc, const char *pcGroupName, UIWidget *pParent, int *y_out)
{
	int y = (*y_out);
	SkyEditorUIElement *pElem = skyEdGetElementUI(pDoc, pcGroupName, "SetRotAxis", NULL);
	UIButton *pButton = pElem->pWidget;

	if(!pButton) {
		pButton = ui_ButtonCreate("", EDITOR_CONTROL_X, y, skyEdSkyDomeRotationAxisButtonEB, pDoc);
		pElem->pWidget = pButton;
		skyEdSetUpWidget(pElem->pWidget, pParent);
	}
	ui_ButtonSetTooltip(pButton, "Uses the camera's current look at angle to set Rotation Axis X, Y and Z. If only one sky dome time, then sets the angle as well.");
	ui_ButtonSetText(pButton, "Set From Camera");

	y += EDITOR_ROW_HEIGHT;

	(*y_out) = y;
}

static int skyEdRefreshGlobalSkyDomePanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyDome *pOrigSelected, SkyDome *pSelected, UIWidget *pParent)
{
	int y = 5;
	MEField *pTempField;
	const char *pcGroupName = "SkyDomeCommon";
	ParseTable *pFuncPrsTbl = parse_SkyDome;

	skyEdRefreshUIAnyWithFunc(kMEFieldType_TextEntry, skyEdSkyDomeDisplayNameChanged, "DisplayName", "Name", "Display name for the sky dome inside the editor." );

	skyEdRefreshSkyDomeType(pDoc, pInfo, pSelected, pParent, y);
	skyEdRefreshUILbl("Type", "What type of sky dome.");

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIFlt("SortOrder",		"Sort Order",		"Determins the order that sky domes get drawn. Smaller values draw in front, larger values draw in the back", 0, 100, 0.01);
	skyEdRefreshUIFIdx("RotationAxis",	"Rotation Axis X",	"Axis that the Sky Dome rotates around.", -1, 1, 0.001, 0);
	skyEdRefreshUIFIdx("RotationAxis",	"Rotation Axis Y",	"Axis that the Sky Dome rotates around.", -1, 1, 0.001, 1);
	skyEdRefreshUIFIdx("RotationAxis",	"Rotation Axis Z",	"Axis that the Sky Dome rotates around.", -1, 1, 0.001, 2);
	skyEdRefreshSkyDomeRotationAxisButton(pDoc, pcGroupName, pParent, &y);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIAny(kMEFieldType_Check, "SunLit",			"Sun Lit",			"Apply the sun as lighting to this sky dome.");
	skyEdRefreshUIAny(kMEFieldType_Check, "HighDetail",		"High Detail",		"Only draw this sky dome if visscale is set above 0.6");
	pTempField = skyEdRefreshUIAny(kMEFieldType_Check, "CharacterOnly",	"Character Only",	"Makes the light coming from this luminary apply only to characters.");
	ui_SetActive(pTempField->pUIWidget, pSelected->luminary2);

	y += EDITOR_ROW_HALF_HEIGHT;
	
	skyEdRefreshSkyDomeTextureSwaps(pDoc, pInfo, pSelected, pParent, &y);

	return y;	
}

static int skyEdRefreshWorldLightPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeSun *pOrigSelected, SkyTimeSun *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeSun);

	skyEdRefreshUIFlt("LightRange",						"Light Range",			"The maximum brightness that should be exposed.", 0, 10, 0.1);
	skyEdRefreshUIHSV("AmbientHSV",						"Ambient",				"Ambient lighting color.");
	skyEdRefreshUIHSV("AmbientHSVAlternate",			"Ambient Alternate",	"Alternate ambient lighting color.");
	skyEdRefreshUIHSV("SkyLightHSV",					"Sky Light",			"Sky hemisphere lighting color.");
	skyEdRefreshUIHSV("GroundLightHSV",					"Ground Light",			"Ground hemisphere lighting color.");
	skyEdRefreshUIHSV("SideLightHSV",					"Side Light",			"Side lighting color.");
	skyEdRefreshUIHSV("DiffuseHSV",						"Diffuse",				"Diffuse color for the sun.");
	skyEdRefreshUIHSV("SpecularHSV",					"Specular",				"Specular color for the sun.");
	skyEdRefreshUIHSV("SecondaryDiffuseHSV",			"Secondary Diffuse",	"Secondary (back) color for the sun.");
	skyEdRefreshUIHSV("ShadowColorHSV",					"Shadow Color",			"Shadow color for the sun's shadows (only in some projects).");
	skyEdRefreshUIFlt("ShadowMinValue",					"Shadow Min Value",		"Minimum value for the sun's shadows (0 = fully shadowed, 1 = fully lit)", 0, 1, 0.01);
	skyEdRefreshUIHSV("BackgroundColorHSV",				"Background Color",		"Background clear color.");
	skyEdRefreshUIAny(kMEFieldType_Check, "Disabled",	"Disabled",				"Completely disabled (for making outdoor maps look like indoor maps).");

	return y;
}

static int skyEdRefreshSecondaryWorldLightPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeSecondarySun *pOrigSelected, SkyTimeSecondarySun *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeSecondarySun);

	skyEdRefreshUIHSV("DiffuseHSV",						"Diffuse",				"Diffuse color for the secondary sun.");
	skyEdRefreshUIHSV("SpecularHSV",					"Specular",				"Specular color for the secondary sun.");
	skyEdRefreshUIHSV("SecondaryDiffuseHSV",			"Secondary Diffuse",	"Secondary (back) color for the secondary sun.");

	return y;
}

static int skyEdRefreshCharLightPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeCharacterLighting *pOrigSelected, SkyTimeCharacterLighting *pSelected, UIWidget *pParent)
{
	int y = 5;
	UILabel *pLabel;
	skyEdRefreshFuncInit(parse_SkyTimeCharacterLighting);

	pLabel = skyEdRefreshUILbl("  Offsets to environment lighting", NULL);
	skyEdMakeOpaqueLabel(pLabel, 255, 126, 0);

	skyEdRefreshUIHSV("AmbientHSVOffset",			"Ambient Offset",		"Character ambient lighting color offset");
	skyEdRefreshUIHSV("skyLightHSVOffset",			"Sky Light Offset",		"Character sky hemisphere lighting color offset");
	skyEdRefreshUIHSV("groundLightHSVOffset",		"Ground Light Offset",	"Character ground hemisphere lighting color offset");
	skyEdRefreshUIHSV("sideLightHSVOffset",			"Side Light Offset",	"Character side lighting color offset");
	skyEdRefreshUIHSV("diffuseHSVOffset",			"Diffuse Offset",		"Character diffuse lighting color offset");
	skyEdRefreshUIHSV("specularHSVOffset",			"Specular Offset",		"Character specular lighting color offset");
	skyEdRefreshUIHSV("secondaryDiffuseHSVOffset",	"Sec Diffuse Offset",	"Character secondary (back) lighting color offset");
	skyEdRefreshUIHSV("shadowColorHSVOffset",		"Shadow Offset",		"Character shadow color offset");
	skyEdRefreshUIHSV("backlightHSV",				"Backlight",			"Character backlight color");

	return y;
}

static int skyEdRefreshCloudShadowPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeCloudShadows *pOrigSelected, SkyTimeCloudShadows *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeCloudShadows);

	skyEdRefreshUIFlt("layer1Multiplier",	"Layer 1 Multiplier",		"Projected shadow texture multiplier for cloud layer 1.", 0, 10, 0.01);
	skyEdRefreshUIFlt("layer1Scale",		"Layer 1 Scale",			"Size of the projected shadow texture in feet for cloud layer 1.", 0, 10000, 1);
	skyEdRefreshUIFIdx("layer1ScrollRate",	"Layer 1 Scroll Rate X",	"Rate at which the projected shadow texture moves in feet per second for cloud layer 1.", -0.1, 0.1, 0.001, 0);
	skyEdRefreshUIFIdx("layer1ScrollRate",	"Layer 1 Scroll Rate Y",	"Rate at which the projected shadow texture moves in feet per second for cloud layer 1.", -0.1, 0.1, 0.001, 1);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIFlt("layer2Multiplier",	"Layer 2 Multiplier",		"Projected shadow texture multiplier for cloud layer 2.", 0, 10, 0.01);
	skyEdRefreshUIFlt("layer2Scale",		"Layer 2 Scale",			"Size of the projected shadow texture in feet for cloud layer 2.", 0, 10000, 1);
	skyEdRefreshUIFIdx("layer2ScrollRate",	"Layer 2 Scroll Rate X",	"Rate at which the projected shadow texture moves in feet per second for cloud layer 2.", -0.1, 0.1, 0.001, 0);
	skyEdRefreshUIFIdx("layer2ScrollRate",	"Layer 2 Scroll Rate Y",	"Rate at which the projected shadow texture moves in feet per second for cloud layer 2.", -0.1, 0.1, 0.001, 1);

	return y;
}

static int skyEdRefreshShadowFadePanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeShadowFade *pOrigSelected, SkyTimeShadowFade *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeShadowFade);

	skyEdRefreshUIFlt("fadeValue",		"Fade Value",	"Shadow value to fade to when turning cloudy to hide the sun angle transitions (0 = fully shadows, 1 = fully lit)", 0, 1, 0.01);
	skyEdRefreshUIFlt("fadeTime",		"Fade Time",	"Time it takes to fade in the ShadowFadeValue when hiding the sun angle transitions.", 0, 100, 0.1);
	skyEdRefreshUIFlt("darkTime",		"Dark Time",	"Amount of time to stay at the ShadowFadeValue when hiding the sun angle transitions.", 0, 100, 0.1);
	skyEdRefreshUIFlt("pulseAmount",	"Pulse Amount",	"How far towards the ShadowFadeValue to pulse the sun shadows (0 = normal shadows, 1 = ShadowFadeValue).", 0, 1, 0.01);
	skyEdRefreshUIFlt("pulseRate",		"Pulse Rate",	"How fast to pulse towards the ShadowFadeValue.", 0, 1, 0.01);

	return y;
}

static int skyEdRefreshColorCorrectionPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeColorCorrection *pOrigSelected, SkyTimeColorCorrection *pSelected, UIWidget *pParent)
{
	int y = 5;
	UILabel *pLabel;
	skyEdRefreshFuncInit(parse_SkyTimeColorCorrection);

	skyEdRefreshUIFlt("LocalContrast",		"Local Contrast",		"Sets the amount of local contrast adjustment.", 0, 10, 0.01);
	skyEdRefreshUIFlt("UnsharpAmount",		"Unsharp Amount",		"Sets the amount of unsharp masking (alternative/additional local contrast adjustment).", 0, 1, 0.01);
	skyEdRefreshUIFlt("UnsharpThreshold",	"Unsharp Threshold",	"Sets the minimum difference require to apply unsharp masking (reduces some artifacts from Unsharp Masking on smooth surfaces).", 0, 0.3, 0.01);

	y += EDITOR_ROW_HALF_HEIGHT;

	pLabel = skyEdRefreshUILbl("  Curves:", "Color correct using curves.");
	skyEdMakeOpaqueLabel(pLabel, 120, 120, 120);
	y = skyEdRefreshCCCurves(pDoc, pInfo, pSelected, pParent, y);
	skyEdRefreshUIRGB("ColorCurveMulti",	"Tint Multiplier",	"Scales the output values for the red, green, and blue curves.");

	y += EDITOR_ROW_HALF_HEIGHT*1.5;

	pLabel = skyEdRefreshUILbl("  Levels:", "Color correct using levels.");
	skyEdMakeOpaqueLabel(pLabel, 120, 120, 120);
	y = skyEdRefreshCCLevels(pDoc, pInfo, pSelected, pParent, y);

	y += EDITOR_ROW_HALF_HEIGHT*1.5;

	pLabel = skyEdRefreshUILbl("  Adjustments:", "Color correct using color adjustments.");
	skyEdMakeOpaqueLabel(pLabel, 120, 120, 120);
	skyEdRefreshUIFlt("ScreenTintSpecificOverlap",	"Overlap",	"Correction per color overlap between specific hues.", 0, 1, 0.01);
	y = skyEdRefreshCCAdjustments(pDoc, pInfo, pSelected, pParent, y);

	return y;
}

static int skyEdRefreshTintPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeTint *pOrigSelected, SkyTimeTint *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeTint);

	skyEdRefreshUIHSV("ScreenTintHSV",			"Screen Tint",			"Fullscreen color adjustment in HSV. Scales each H, S, and V value.");
	skyEdRefreshUIHSV("ScreenTintOffsetHSV",	"Screen Tint Offset",	"Fullscreen color adjustment in HSV. Adds to H, S, and V value.");

	return y;
}

static int skyEdRefreshOutlinePanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeOutline *pOrigSelected, SkyTimeOutline *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeOutline);

	skyEdRefreshUIFlt("OutlineLuminance1",	"Luminance 1",			"Scene luminance at which to use the first outline color.", 0, 10, 0.1);
	skyEdRefreshUIHSV("OutlineHSV",			"Color 1",				"Outline color at luminance 1.");
	skyEdRefreshUIFlt("OutlineLuminance2",	"Luminance 2",			"Scene luminance at which to use the second outline color.", 0, 10, 0.1);
	skyEdRefreshUIHSV("OutlineHSV2",		"Color 2",				"Outline color at luminance 2.");

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIFlt("OutlineAlpha",		"Alpha",				"Outline Transparency", 0, 1, 0.01);
	skyEdRefreshUIFIdx("OutlineFade",		"Depth Test Start",		"Outline Fade Depths: Start fade Z for depth-test.",	0, 5000, 0.5, 0);
	skyEdRefreshUIFIdx("OutlineFade",		"Depth Test End",		"Outline Fade Depths: End fade Z for depth-test.",		0, 5000, 0.5, 1);
	skyEdRefreshUIFIdx("OutlineFade",		"Normal Test Start",	"Outline Fade Depths: Start fade Z for normal test.",	0, 10000, 1, 2);
	skyEdRefreshUIFIdx("OutlineFade",		"Normal Test End",		"Outline Fade Depths: End fade Z for normal test.",		0, 10000, 1, 3);
	skyEdRefreshUIFlt("OutlineThickness",	"Thickness",			"Outline Thickness", 1, 5, 1);

	skyEdRefreshUIAny(kMEFieldType_BooleanCombo, "UseZ",			"Use Z",				"Use Z-based depth buffer values for detection.");

	return y;
}

static int skyEdRefreshFogPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeFog *pOrigSelected, SkyTimeFog *pSelected, UIWidget *pParent)
{
	int y = 5;
	MEField *pField;
	skyEdRefreshFuncInit(parse_SkyTimeFog);

	skyEdRefreshUIHSV("ClipFogColorHSV",		"Clip Fog",				"Fog color to be used when fog clipping is on");
	skyEdRefreshUIHSV("ClipBackgroundColorHSV",	"Clip Background",		"Background color to be used when fog clipping is on");
	skyEdRefreshUIFlt("ClipFogDistanceAdjust",	"Clip Fog Dist Adjust",	"Amount to adjust the fogClipDist on low end, use this only with permission, may have a serious performance impact", 0, 3, 0.25);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUILbl("Fog Height:", "Height positions for the fog.");
	pDoc->UI.pFogHeightLow = skyEdRefreshUIFIdx("FogHeight",	"      Low",		"Low fog height that determine which fog color and distance to use",	-10000, 10000, 1, 0);
	pDoc->UI.pFogHeightLow->pUISliderText->pSlider->bias = 3;
	pDoc->UI.pFogHeightLow->pUISliderText->pSlider->bias_offset = 0.5;
	pDoc->UI.pFogHeightHigh = skyEdRefreshUIFIdx("FogHeight",	"      High",		"Hight fog height that determine which fog color and distance to use",	-10000, 10000, 1, 1);
	pDoc->UI.pFogHeightHigh->pUISliderText->pSlider->bias = 3;
	pDoc->UI.pFogHeightHigh->pUISliderText->pSlider->bias_offset = 0.5;

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUILbl("Low Fog:", "Low fog values.");
	pField = skyEdRefreshUIFIdx("FogDist",				"      Dist Near",	"Low fog distance near",	-10000, 10000, 1, 0);
	pField->pUISliderText->pSlider->bias = 3;
	pField->pUISliderText->pSlider->bias_offset = 0.5;
	pField = skyEdRefreshUIFIdx("FogDist",				"      Dist Far",	"Low fog distance far.",	-10000, 10000, 1, 1);
	pField->pUISliderText->pSlider->bias = 3;
	pField->pUISliderText->pSlider->bias_offset = 0.5;
	skyEdRefreshUIFlt("FogMax",					"      Max",		"The maximum amount of fog allowed for the low fog (0 - 1)", 0, 1, 0.01);
	skyEdRefreshUIHSV("FogColorHSV",			"      Color",		"Fog color to be used at low heights");

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUILbl("High Fog:", "Low fog values.");
	pField = skyEdRefreshUIFIdx("HighFogDist",			"      Dist Near",	"High fog distance near.",	-10000, 10000, 1, 0);
	pField->pUISliderText->pSlider->bias = 3;
	pField->pUISliderText->pSlider->bias_offset = 0.5;
	pField = skyEdRefreshUIFIdx("HighFogDist",			"      Dist Far",	"High fog distance far.",	-10000, 10000, 1, 1);
	pField->pUISliderText->pSlider->bias = 3;
	pField->pUISliderText->pSlider->bias_offset = 0.5;
	skyEdRefreshUIFlt("HighFogMax",				"      Max",		"The maximum amount of fog allowed for the high fog (0 - 1)", 0, 1, 0.01);
	skyEdRefreshUIHSV("HighFogColorHSV",		"      Color",		"Fog color to be used at high heights");

//	Disabling this until light scattering and fog are fully set up
//
// 	y += EDITOR_ROW_HALF_HEIGHT;
// 
// 	skyEdRefreshUIFIdx("FogDensity",			"Extinction Rate",		"Fog light extinction rate.", 0, 1, 0.01, 0);
// 	skyEdRefreshUIFIdx("FogDensity",			"Scattering Rate",		"Fog ambient scattering rate.", 0, 1, 0.01, 1);
// 	skyEdRefreshUIFIdx("VolumeFogPos",			"Volume Fog Pos X",		"Center of volumetric fog, X component.", -10000, 10000, 1, 0);
// 	skyEdRefreshUIFIdx("VolumeFogPos",			"Volume Fog Pos Y",		"Center of volumetric fog, X component.", -10000, 10000, 1, 1);
// 	skyEdRefreshUIFIdx("VolumeFogPos",			"Volume Fog Pos Z",		"Center of volumetric fog, X component.", -10000, 10000, 1, 2);
// 	skyEdRefreshUIFIdx("VolumeFogScale",		"Volume Fog Scale X",	"Radii of volumetric fog ellipsoid.", 0, 1, 0.01, 0);
// 	skyEdRefreshUIFIdx("VolumeFogScale",		"Volume Fog Scale Y",	"Radii of volumetric fog ellipsoid.", 0, 1, 0.01, 1);
// 	skyEdRefreshUIFIdx("VolumeFogScale",		"Volume Fog Scale Z",	"Radii of volumetric fog ellipsoid.", 0, 1, 0.01, 2);

	return y;
}

static int skyEdRefreshLightBehaviorPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeLightBehavior *pOrigSelected, SkyTimeLightBehavior *pSelected, UIWidget *pParent)
{
	int y = 5;
	UILabel *pLabel;
	skyEdRefreshFuncInit(parse_SkyTimeLightBehavior);

	pLabel = skyEdRefreshUILbl("  Don't edit these settings without permission!", "");
	skyEdMakeOpaqueLabel(pLabel, 255, 0, 0);

	skyEdRefreshUIFlt("LightRange",				"Light Range",				"The maximum brightness that should be exposed.", 0, 10, 0.1);
	skyEdRefreshUIFlt("LightAdaptation",		"Light Adaptation",			"How much to use the sampled scene luminance to do light adaptation; 0 turns light adaptation off and 1 turns it completely on.", 0, 1, 0.01);
	skyEdRefreshUIFlt("LightAdaptationRate",	"Light Adaptation Rate",	"Specifies the amount of time (in seconds) the HDR system will take to adjust the light range by 1.", 0, 1000, 0.01);
	skyEdRefreshUIFlt("Exposure",				"Exposure",					"Modifies the grey point of the light adaptation.  A value of 0.5 is no change; lower values darken the scene and higher values brighten the scene.", 0, 2, 0.01);
	skyEdRefreshUIFlt("BlueshiftMin",			"Blueshift Min",			"Specifies the luminance value at which colors should be fully desaturated.", 0, 1, 0.01);
	skyEdRefreshUIFlt("BlueshiftMax",			"Blueshift Max",			"Specifies the luminance value at which colors should start desaturating.  At luminance values above this value colors will be fully saturated.", 0, 1, 0.01);
	skyEdRefreshUIHSV("BlueshiftHSV",			"Blueshift Color",			"Specifies the HSV color to which colors in the blueshift range should shift towards.");

	return y;
}

static int skyEdRefreshBloomPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeBloom *pOrigSelected, SkyTimeBloom *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeBloom);

	skyEdRefreshUIFlt("BloomOffsetValue",			"Offset Value",				"The brightness past the light range at which bloom starts.", -4.0f, 4.0f, 0.0f);
	skyEdRefreshUIFlt("BloomRate",					"Rate",						"The rate at which bloom gets brighter.  0 to disable bloom.", 0.0f, 1.0f, 0.0f);
	skyEdRefreshUIFlt("BloomRange",					"Range",					"The range of brightnesses that can bloom.  Affects the bloom value precision.", 0.0f, 10.0f, 0.0f);
	skyEdRefreshUIFIdx("BloomBlurAmount",			"Blur High",				"Specifies the multiplier for the high blur level to be added together into the bloom texture.", 0.0f, 10.0f, 0.0f, 0);
	skyEdRefreshUIFIdx("BloomBlurAmount",			"Blur Medium",				"Specifies the multiplier for the medium blur level to be added together into the bloom texture.", 0.0f, 10.0f, 0.0f, 1);
	skyEdRefreshUIFIdx("BloomBlurAmount",			"Blur Low",					"Specifies the multiplier for the low blur level to be added together into the bloom texture.", 0.0f, 10.0f, 0.0f, 2);
	skyEdRefreshUIFlt("glareAmount",				"Glare Amount",				"Multiplier for glare when it is added to the final image.  0 to disable glare.", 0.0f, 10.0f, 0.1f);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUILbl("Low Quality Values:", "Values for low end machines.");

	skyEdRefreshUIFlt("lowQualityBloomStart",		"Start",					"The brightness (in screen brightness) at which bloom starts for low quality bloom.", 0.0f, 1.0f, 0.0001f);
	skyEdRefreshUIFlt("lowQualityBloomMultiplier",	"Multiplier",				"Brightness multiplier applied after LowQualityBloomStart is subtracted from the screen brightness.", 0.0f, 10.0f, 0.0f);
	skyEdRefreshUIFlt("lowQualityBloomPower",		"Power",					"The exponent to which the offset and multiplied brightness is raised before getting downsampled and blurred.", 0.0f, 16.0f, 0.0f);

	return y;
}

static int skyEdRefreshDOFPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, DOFValues *pOrigSelected, DOFValues *pSelected, UIWidget *pParent)
{
	int y = 5;
	MEField *pField;
	skyEdRefreshFuncInit(parse_DOFValues);

	skyEdRefreshUILbl("Distances:", "Distances for each range.");

	pField = skyEdRefreshUIFlt("nearDist",		"      Near",		"The weight of the depth-of-field blur equals nearValue at this depth.", 0, 10000, 0.01);
	pField->pUISliderText->pSlider->bias = 3;
	pField = skyEdRefreshUIFlt("focusDist",		"      Focus",		"This is the depth of the focal plane of the camera. The blur weight is focusValue at this depth, and is typically zero or a low blur weight because this depth should be (but is not required to be) in focus.", 0, 10000, 0.01);
	pField->pUISliderText->pSlider->bias = 3;
	pField = skyEdRefreshUIFlt("farDist",		"      Far",		"All depths equal-to or further-than this depth have the blur factor farValue.", 0, 10000, 0.01);
	pField->pUISliderText->pSlider->bias = 3;

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUILbl("Strengths:", "Anount of blur for each range and the sky itself.");

	skyEdRefreshUIFlt("nearValue",				"      Near",		"At this depth and closer, the depth-of-field has this blur factor, ranged [0-1], where one is maximum blur.", 0, 1, 0.01);
	skyEdRefreshUIFlt("focusValue",				"      Focus",		"The in-focus blur factor. See nearValue for range and interpretation.", 0, 1, 0.01);
	skyEdRefreshUIFlt("farValue",				"      Far",		"The furthest blur factor. See nearValue for range and interpretation", 0, 1, 0.01);
	skyEdRefreshUIFlt("skyValue",				"      Sky",		"The blur factor for the sky. See nearValue for range and interpretation.", 0, 1, 0.01);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIHSV("borderColorHSV",			"Border Color",			"Additive border color (can be negative).");
	skyEdRefreshUIFlt("borderColorScale",		"Border Color Scale",	"Scale on the border color for allowing negative colors.", -1, 1, 0.01);
	skyEdRefreshUIFlt("borderRamp",				"Border Ramp",			"Border ramp.  Smaller values start the border nearer the edge.", 0, 10, 0.01);
	skyEdRefreshUIFlt("borderBlur",				"Border Blur",			"Amount of blur to apply to border pixels.", 0, 2, 0.01);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIHSV("depthAdjustFgHSV",		"Adjust Foreground",	"Saturation and value adjustment for the foreground.");
	skyEdRefreshUIHSV("depthAdjustBgHSV",		"Adjust Background",	"Saturation and value adjustment for the background.");
	skyEdRefreshUIHSV("depthAdjustSkyHSV",		"Adjust Sky",			"Saturation and value adjustment for the sky.");
	pField = skyEdRefreshUIFlt("depthAdjustNearDist",	"Adjust Near Dist",		"Distance to start background adjustment.", 0, 10000, 0.01);
	pField->pUISliderText->pSlider->bias = 3;
	pField = skyEdRefreshUIFlt("depthAdjustFadeDist",	"Adjust Fade Dist",		"Distance across which to fade from FG to BG.", 0, 10000, 0.01);
	pField->pUISliderText->pSlider->bias = 3;

	return y;
}

static int skyEdRefreshAmbientOcclusionPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeAmbientOcclusion *pOrigSelected, SkyTimeAmbientOcclusion *pSelected, UIWidget *pParent)
{
	int y = 5;

	skyEdRefreshFuncInit(parse_SkyTimeAmbientOcclusion);

	skyEdRefreshUIFlt("worldSampleRadius",	"Sample Radius",	"The radius in feet to sample within for the world-space based SSAO term.", 0, 10, 0.01);
	skyEdRefreshUIFlt("worldSampleFalloff",	"Sample Falloff",	"The falloff rate for the world-space based SSAO term.", 0, 10, 0.1);
	skyEdRefreshUIFlt("worldSampleScale",	"Sample Scale",		"The scale multiplier for the world-space based SSAO term.", 0, 10, 0.1);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIFlt("overallScale",		"Overall Scale",	"The overall scale for the SSAO term.", 0, 10, 0.1);
	skyEdRefreshUIFlt("overallOffset",		"Overall Offset",	"The overall offset for the SSAO term (subtracted from the SSAO term.", 0, 10, 0.1);

	y += EDITOR_ROW_HALF_HEIGHT;

	skyEdRefreshUIFlt("litAmount",			"Lit Amount",		"The amount of SSAO used for non-ambient light", 0, 10, 0.1);

	return y;
}

static int skyEdRefreshWindPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeWind *pOrigSelected, SkyTimeWind *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeWind);

	skyEdRefreshUIFlt("Speed",					"Speed",					"The speed of the wind", 0, 10, 0.1);
	skyEdRefreshUIFlt("SpeedVariation",			"Speed Variation",			"The amount of variation in the speed of the wind.", 0, 10, 0.1);
	skyEdRefreshUIFIdx("Direction",				"Direction X",				"The direction of wind, X component.", -1, 1, 0.001, 0);
	skyEdRefreshUIFIdx("Direction",				"Direction Y",				"The direction of wind, Y component.", -1, 1, 0.001, 1);
	skyEdRefreshUIFIdx("Direction",				"Direction Z",				"The direction of wind, Z component.", -1, 1, 0.001, 2);
	skyEdRefreshUIFIdx("DirectionVariation",	"Direction Variation X",	"The amount of variation in the X direction of wind.", 0, 1, 0.001, 0);
	skyEdRefreshUIFIdx("DirectionVariation",	"Direction Variation Y",	"The amount of variation in the Y direction of wind.", 0, 1, 0.001, 1);
	skyEdRefreshUIFIdx("DirectionVariation",	"Direction Variation Z",	"The amount of variation in the Z direction of wind.", 0, 1, 0.001, 2);
	skyEdRefreshUIFlt("Turbulence",				"Turbulence",				"The speed at which the wind direction and speed changes.", 0, 1, 0.01);
	skyEdRefreshUIFlt("variationScale",			"Variation Scale",			"The amount world units are scaled before they are used as input to the random noise generator.", 0, 1, 0.001);

	return y;
}

static int skyEdRefreshSkyScatteringPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyTimeScattering *pOrigSelected, SkyTimeScattering *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyTimeScattering);

	skyEdRefreshUIFIdx("Parameters",	"Light Increase Rate",	"Rate of light increase per unit distance when in lit area.", 0, 0.1, 0.001, 0);
	skyEdRefreshUIFIdx("Parameters",	"Light Decrease Rate",	"Rate of light decrease per unit distance due to scattering media.", -0.1, 0, 0.001, 1);
	skyEdRefreshUIFIdx("Parameters",	"Scale Factor",			"Scale factor for scatter brightness.", 0, 2, 0.01, 2);
	skyEdRefreshUIHSV("LightColor",		"Light Color",			"Color of light due to scattering.");

	return y;
}

static int skyEdRefreshSkyDomeTimePanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyDomeTime *pOrigSelected, SkyDomeTime *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_SkyDomeTime);

	skyEdRefreshUIFlt("Alpha",			"Alpha",			"Alpha value (So you can make clouds partially fade at night)", 0, 1, 0.01);
	skyEdRefreshUIFlt("Scale",			"Scale",			"Scale value (So you can shrink or grow a moon or a sun)", 0.01, 10, 0.01);
	skyEdRefreshUIFlt("Angle",			"Angle",			"Angle around the RotationAxis that the sky dome has rotated", -360, 360, 0.1);
	skyEdRefreshUIHSV("TintHSV",		"Tint HSV",			"Tint applied to geo");
	skyEdRefreshUIFIdx("Position",		"Position X",		"X position of the SkyDome", -30000, 30000, 0.1, 0);
	skyEdRefreshUIFIdx("Position",		"Position Y",		"Y position of the SkyDome", -30000, 30000, 0.1, 1);
	skyEdRefreshUIFIdx("Position",		"Position Z",		"Z position of the SkyDome", -30000, 30000, 0.1, 2);
	skyEdRefreshUIHSV("AmbientHSV",		"Ambient HSV",		"Ambient color to use on the Sky Dome");
	skyEdRefreshUIFlt("AmbientWeight",	"Ambient Weight",	"Internal value", 0, 1, 0.01);
//	MaterialNamedConstant	**mat_props;	AST( NAME(MaterialProperty) WIKI("Material op name + RGBA") )	skyEdRefreshUIFIdx("Parameters",	"Light Increase Rate",	"Rate of light increase per unit distance when in lit area.", 0, 1, 0.001, 0);

	return y;
}

static int skyEdRefreshShadowRulesPanel(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, SkyDomeTime *pOrigSelected, SkyDomeTime *pSelected, UIWidget *pParent)
{
	int y = 5;
	skyEdRefreshFuncInit(parse_ShadowRules);

	skyEdRefreshUIFlt("CameraFalloffDist",			"Camera Falloff Dist",			"Camera Falloff Dist", 0, 2*MAX_PLAYABLE_DIST_ORIGIN_SQR, 0);
	skyEdRefreshUIAny(kMEFieldType_Check,			"CameraScaleFalloff",		    "Camera Scale Falloff",		"Apply Camera Scale Falloff?");

	skyEdRefreshUIFIdx("ThreeSplitDistances",		"Split Distance Near",			"High-Res Shadows stop at this distance from camera",   0, 2*MAX_PLAYABLE_DIST_ORIGIN_SQR, 0, 0);
	skyEdRefreshUIFIdx("ThreeSplitDistances",		"Split Distance Average",		"Medium-Res Shadows stop at this distance from camera", 0, 2*MAX_PLAYABLE_DIST_ORIGIN_SQR, 0, 1);
	skyEdRefreshUIFIdx("ThreeSplitDistances",		"Split Distance Far",			"Low-Res Shadows stop at this distance from camera",    0, 2*MAX_PLAYABLE_DIST_ORIGIN_SQR, 0, 2);

	return y;
}

static void skyEdBlockTimeChangedCB(UITimelineKeyFrame *pFrame, SkyBlockInfo *pInfo)
{
	GenericSkyBlock *pBlock = pFrame->data;
	pBlock->time = SE_TrackTimeToSky(pFrame->time);
	pBlock->time = CLAMP(pBlock->time, 0.0f, 24.0f);
}

static UITimelineKeyFrame* skyEdMakeFrame(UITimelineTrack *pTrack)
{
	UITimelineKeyFrame *pFrame = ui_TimelineKeyFrameCreate();
	ui_TimelineTrackAddFrame(pTrack, pFrame);	
	return pFrame;
}

static GenericSkyBlock* skyEdGetOrigData(SkyBlockInfo *pInfo, GenericSkyBlock *pBlock)
{
	int idx;
	GenericSkyBlock ***pppBlocks;
	GenericSkyBlock ***pppOrigBlocks;
	SkyEditorDoc *pDoc = pInfo->pDoc;

	if(!pDoc->pOrigSkyInfo)
		return NULL;

	pppBlocks = skyEdGetInfoTimesEx(pInfo, pDoc->pSkyInfo);
	pppOrigBlocks = skyEdGetInfoTimesEx(pInfo, pDoc->pOrigSkyInfo);
	if(!pppOrigBlocks)
		return NULL;

	idx = eaFind(pppBlocks, pBlock);
	if(idx >= 0 && idx < eaSize(pppOrigBlocks)) 
		return (*pppOrigBlocks)[idx];
	return NULL;
}

static SkyBlockInfo* skyEdGetSelectedSkyDomeInfo(SkyEditorDoc *pDoc)
{
	int i;
	SkyBlockInfo *pSelectedInfo = NULL;

	//This is basically trying to find the info for the selected sky dome time,
	//if and only if one sky dome time is selected.
	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
		if(!pInfo->bIsSkyDome)
			continue;
		if(eaSize(&pInfo->ppSelected) > 0) {
			if(pSelectedInfo) {
				pSelectedInfo = NULL;
				break;
			}
			pSelectedInfo = pInfo;
		}
	}

	return pSelectedInfo;
}

static void skyEdRefreshBlockUI(SkyBlockInfo *pInfo)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;
	SkyBlockInfo *pSelectedDomeInfo = skyEdGetSelectedSkyDomeInfo(pDoc);
	GenericSkyBlock **ppSkyBlocks = *skyEdGetInfoTimes(pInfo);
	UITimelineTrack *pTrack = pInfo->pTrack;
	int iBlockCnt = eaSize(&ppSkyBlocks);

	ui_TimelineRemoveTrack(pDoc->UI.pTimeline, pTrack);
	eaFindAndRemove(&pDoc->emDoc.em_panels, pInfo->pPanel);

	if(iBlockCnt > 0) {
		int i;
		UITimelineKeyFrame *pFrame;

		ui_TimelineAddTrack(pDoc->UI.pTimeline, pTrack);
		if(eaSize(&pInfo->ppSelected) == 1 && (!pInfo->bIsSkyDome || pSelectedDomeInfo)) {
			GenericSkyBlock *pOrigBlock = skyEdGetOrigData(pInfo, pInfo->ppSelected[0]);
			int height = pInfo->refreshFunc(pDoc, pInfo, pOrigBlock, pInfo->ppSelected[0], UI_WIDGET(pInfo->pPane));
			emPanelSetHeight(pInfo->pPanel, height);
			eaPush(&pDoc->emDoc.em_panels, pInfo->pPanel);
			
			{//Update EM Expanders if needed
				static EMPanel *pPrevPanel = NULL;
				if(pInfo->pPanel != pPrevPanel) {
					pPrevPanel = pInfo->pPanel;
					emRefreshDocumentUI();
				}
			}
		}

		for ( i=0; i < iBlockCnt; i++ ) {
			GenericSkyBlock *pBlock = ppSkyBlocks[i];

			pFrame = ui_TimelineTrackGetFrame(pTrack, i);
			if(!pFrame) {
				pFrame = skyEdMakeFrame(pTrack);
			}

			pFrame->data = pBlock;
			pFrame->time = (iBlockCnt == 1 ? 0 : SE_SkyTimeToTrack(pBlock->time));
			pFrame->length = (iBlockCnt == 1 ? 20000 : 1);
			ui_TimelineKeyFrameSetSelected(pFrame, eaFind(&pInfo->ppSelected, pBlock) >= 0);
			ui_TimelineKeyFrameSetPreChangedCallback(pFrame, skyEdTimelineFramePreChangedCB, pDoc);
			ui_TimelineKeyFrameSetChangedCallback(pFrame, skyEdBlockTimeChangedCB, pInfo);
		}

		while(pFrame = ui_TimelineTrackGetFrame(pTrack, i)) {
			ui_TimelineTrackRemoveFrame(pTrack, pFrame);
			ui_TimelineKeyFrameFree(pFrame);
		}
	}
}

static void skyEdRefreshAllBlocksUI(SkyEditorDoc *pDoc)
{
	int i;
	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		skyEdRefreshBlockUI(pDoc->ppBlockInfos[i]);
	}
}

static void skyEdRefreshGeneralPanel(SkyEditorDoc *pDoc)
{
	MEFieldContext *pContext;

	pContext = MEContextPush("SkyEditor_General", pDoc->pOrigSkyInfo, pDoc->pSkyInfo, parse_SkyInfo);
	pContext->pUIContainer = UI_WIDGET(pDoc->UI.pGeneralPane);
	pContext->cbPreChanged = skyEdMEFieldPreChangeCB;
	pContext->pPreChangedData = pDoc;
	pContext->cbChanged = skyEdMEFieldChangeCB;
	pContext->pChangedData = pDoc;

	MEContextAddSimple(kMEFieldType_TextEntry,		"Tags",							"Tags",						"Tags for searching, genesis and UGC.");
	MEContextAddPicker("SkyInfo", "Sky Picker",		"NoPPFallback",					"No Post Process Fallback",	"Sky to fallback to if post processing is turned off.");			

	MEContextAddSimple(kMEFieldType_Check,			"IgnoreFogClip",				"Ignore Fog Clip Far",		"Ignore this sky's Fog Clip Far setting.");
	MEContextAddSimple(kMEFieldType_Check,			"FogClip",						"Fog Clip Far",				"Clip all objects beyond the fog distance.  Automatically sets the background color equal to the fog color.");
	MEContextAddSimple(kMEFieldType_Check,			"IgnoreFogClipLow",				"Ignore Fog Clip Low",		"Ignore this sky's Fog Clip Low setting.");
	MEContextAddSimple(kMEFieldType_Check,			"FogClipLow",					"Fog Clip Low",				"Clip all objects beyond the fog distance if they are below the camera.  Automatically sets the background color equal to the fog color.");

	MEContextAddSimple(kMEFieldType_Texture,		"CloudShadowTexture",			"Cloud Shadow",				"The name of the projected shadow texture to use for all cloud layers.");
	MEContextAddSimple(kMEFieldType_Texture,		"DiffuseWarpTextureCharacter",	"Diffuse Warp Character",	"The name of the diffuse warping texture to use for character lighting.");
	MEContextAddSimple(kMEFieldType_Texture,		"DiffuseWarpTextureWorld",		"Diffuse Warp World",		"The name of the diffuse warping texture to use for world lighting.");
	MEContextAddSimple(kMEFieldType_Texture,		"AmbientCube",					"Ambient Cube",				"The name of the ambient cubemap texture to use for ambient lighting on the materials that want it.");
	MEContextAddSimple(kMEFieldType_Texture,		"ReflectionCube",				"Reflection Cube",			"The name of the reflection cubemap texture to use if not specified in the region or material.");

	emPanelSetHeight(pDoc->UI.pGeneralPanel, pContext->iYPos);
	MEContextPop("SkyEditor_General");
}

static void skyEdRefreshTimelinePanel(SkyEditorDoc *pDoc)
{
	int i;
	bool static_sky = true;
	for ( i=0; i < eaSize(&pDoc->ppBlockInfos); i++ ) {
		SkyBlockInfo *pInfo = pDoc->ppBlockInfos[i];
		GenericSkyBlock ***pppTimes = skyEdGetInfoTimes(pInfo);
		if(pppTimes && eaSize(pppTimes) > 1) {
			static_sky = false;
			break;
		}
	}

	if(static_sky)
		ui_WidgetSetWidthEx(UI_WIDGET(pDoc->UI.pTimelinePane), 0.2, UIUnitPercentage);
	else 
		ui_WidgetSetWidthEx(UI_WIDGET(pDoc->UI.pTimelinePane), 1, UIUnitPercentage);
}

static void skyEdRefreshSkyDomePanel(SkyEditorDoc *pDoc)
{
	SkyBlockInfo *pSelectedInfo = skyEdGetSelectedSkyDomeInfo(pDoc);

	eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeCommonPanel);
	eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeLuminaryPanel);
	eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeObjectPanel);
	eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeStarFieldPanel);
	eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeAtmospherePanel);
	eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeLensFlarePanel);

	if(pSelectedInfo) {
		SkyDome *pSkyDome;
		SkyDome *pOrigSkyDome = NULL;
		int iSkyDomeIdx = pSelectedInfo->iSkyDomeIdx;
		
		assert(iSkyDomeIdx >= 0 && iSkyDomeIdx < eaSize(&pDoc->pSkyInfo->skyDomes));
		pSkyDome = pDoc->pSkyInfo->skyDomes[iSkyDomeIdx];

		if(iSkyDomeIdx < eaSize(&pDoc->pOrigSkyInfo->skyDomes)) {
			pOrigSkyDome = pDoc->pOrigSkyInfo->skyDomes[iSkyDomeIdx];
		}

		{
			int height = skyEdRefreshGlobalSkyDomePanel(pDoc, pSelectedInfo, pOrigSkyDome, pSkyDome, UI_WIDGET(pDoc->UI.pSkyDomeCommonPane));
			emPanelSetHeight(pDoc->UI.pSkyDomeCommonPanel, height);
			eaPush(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeCommonPanel);
		}

		if(pSkyDome->star_field) {
			int height = skyEdRefreshSkyDomeStarFieldPanel(pDoc, pSelectedInfo, SAFE_MEMBER(pOrigSkyDome, star_field), pSkyDome->star_field, UI_WIDGET(pDoc->UI.pSkyDomeStarFieldPane));
			emPanelSetHeight(pDoc->UI.pSkyDomeStarFieldPanel, height);
			eaPush(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeStarFieldPanel);
		} else if (pSkyDome->atmosphere) {
			int height = skyEdRefreshSkyDomeAtmospherePanel(pDoc, pSelectedInfo, SAFE_MEMBER(pOrigSkyDome, atmosphere), pSkyDome->atmosphere, UI_WIDGET(pDoc->UI.pSkyDomeAtmospherePane));
			emPanelSetHeight(pDoc->UI.pSkyDomeAtmospherePanel, height);
			eaPush(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeAtmospherePanel);
		} else if (pSkyDome->luminary) {
			int height = skyEdRefreshSkyDomeLuminaryPanel(pDoc, pSelectedInfo, pOrigSkyDome, pSkyDome, UI_WIDGET(pDoc->UI.pSkyDomeLuminaryPane));
			emPanelSetHeight(pDoc->UI.pSkyDomeLuminaryPanel, height);
			eaPush(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeLuminaryPanel);

			if(pSkyDome->lens_flare && pSelectedInfo->iSelectedLensFlare >= 0 && pSelectedInfo->iSelectedLensFlare < eaSize(&pSkyDome->lens_flare->flares)) {
				LensFlarePiece *pPiece = pSkyDome->lens_flare->flares[pSelectedInfo->iSelectedLensFlare];
				LensFlarePiece *pOrigPiece = NULL;
				if(pOrigSkyDome && pOrigSkyDome->lens_flare && pSelectedInfo->iSelectedLensFlare < eaSize(&pOrigSkyDome->lens_flare->flares))
					pOrigPiece = pOrigSkyDome->lens_flare->flares[pSelectedInfo->iSelectedLensFlare];
				height = skyEdRefreshSkyDomeLensFlarePanel(pDoc, pSelectedInfo, pOrigPiece, pPiece, UI_WIDGET(pDoc->UI.pSkyDomeLensFlarePane));
				emPanelSetHeight(pDoc->UI.pSkyDomeLensFlarePanel, height);
				eaPush(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeLensFlarePanel);	
			}
		} else {
			int height = skyEdRefreshSkyDomeObjectPanel(pDoc, pSelectedInfo, pOrigSkyDome, pSkyDome, UI_WIDGET(pDoc->UI.pSkyDomeObjectPane));
			emPanelSetHeight(pDoc->UI.pSkyDomeObjectPanel, height);
			eaPush(&pDoc->emDoc.em_panels, pDoc->UI.pSkyDomeObjectPanel);
		} 
	}
}

static void skyEdRefreshUI(SkyEditorDoc *pDoc)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	pDoc->bRefreshingUI = true;

	pCamera->time_override = SE_TrackTimeToSky(ui_TimelineGetTime(pDoc->UI.pTimeline));

	skyEdRefreshTimelinePanel(pDoc);
	skyEdRefreshGeneralPanel(pDoc);
	skyEdRefreshSkyDomePanel(pDoc);
	skyEdRefreshAllBlocksUI(pDoc);

	StructCopyAll(parse_SkyInfo, pDoc->pSkyInfo, gfxSkyFindSky(pDoc->pSkyInfo->filename_no_path));
	gfxSkyClearAllVisibleSkies();
	gfxInvalidateAllLightCaches();

	if(pDoc->pSkyInfo->filename)
	{
		ui_SetActive(UI_WIDGET(pDoc->UI.pFileButton), true);
		ui_GimmeButtonSetName(pDoc->UI.pFileButton, pDoc->emDoc.doc_name);
		ui_GimmeButtonSetReferent(pDoc->UI.pFileButton, pDoc->pSkyInfo);
		ui_LabelSetText(pDoc->UI.pFilenameLabel, pDoc->pSkyInfo->filename);
	}
	else
	{
		ui_SetActive(UI_WIDGET(pDoc->UI.pFileButton), false);
		ui_LabelSetText(pDoc->UI.pFilenameLabel, "");
	}

	pDoc->bRefreshingUI = false;
}

static void skyEdAddSkyNewSkyBlockTime(UITimeline *pTrack, int timelineTime, SkyBlockInfo *pInfo)
{
	SkyEditorDoc *pDoc = pInfo->pDoc;
	GenericSkyBlock ***pppBlocks = skyEdGetInfoTimes(pInfo);
	GenericSkyBlock *pNewBlock = NULL;
	F32 fTime;

	if(!skyEdIsEditable(pDoc))
		return;

	pNewBlock = StructCreateVoid(pInfo->pBlockPti);
	fTime = SE_TrackTimeToSky(timelineTime);

	if(eaSize(pppBlocks)==0) {
		GfxCameraView *pCameraView = gfxGetActiveCameraView();
		BlendedSkyInfo *pBlended;
		void *pBlendedBlock;
		assert(pCameraView && pCameraView->sky_data && pCameraView->sky_data->visible_sky);
		pBlended = pCameraView->sky_data->visible_sky;
		pBlendedBlock = TokenStoreGetPointer(parse_BlendedSkyInfo, pInfo->iBlendedColumn, pBlended, 0, NULL);		
		assert(pBlendedBlock);
		StructCopyAllVoid(pInfo->pBlockPti, pBlendedBlock, pNewBlock);
	} else if(eaSize(pppBlocks)==1) {
		StructCopyAllVoid(pInfo->pBlockPti, (*pppBlocks)[0], pNewBlock);
	} else {
		int early_idx;
		int late_idx;
		F32 ratio;

		gfxSkyFindValuesSurroundingTime(*pppBlocks, eaSize(pppBlocks), fTime, &early_idx, &late_idx, &ratio);

		//Blend values to current time
		StructCopyAllVoid(pInfo->pBlockPti, (*pppBlocks)[early_idx], pNewBlock);
		shDoOperationSetFloat(ratio);
		shDoOperation(STRUCTOP_LERP, pInfo->pBlockPti, pNewBlock, (*pppBlocks)[late_idx]);	
	}
	
	pNewBlock->time = fTime;
	eaPush(pppBlocks, pNewBlock);
	eaQSort(*pppBlocks, skyEdSortBlocks);
	skyEdClearSelection(pDoc);
	eaPush(&pInfo->ppSelected, pNewBlock);
	ui_TimelineSetTime(pDoc->UI.pTimeline, SE_SkyTimeToTrack(pNewBlock->time));
	skyEdRefreshUI(pDoc);
	skyEdSetUnsaved(pDoc);	
}

static void skyEdSetDomeDisplayNameUnique(SkyEditorDoc *pDoc, SkyDome *pDomeIn, const char *pcName)
{
	int i;
	int iAppendNum = 0;
	char pcNewName[256];
	if(!pcName || !pcName[0])
		pcName = "Sky Dome";
	strcpy(pcNewName, pcName);
	for ( i=0; i < eaSize(&pDoc->pSkyInfo->skyDomes); i++ ) {
		SkyDome *pDome = pDoc->pSkyInfo->skyDomes[i];
		if(pDomeIn == pDome) 
			continue;
		if(stricmp_safe(pDome->display_name, pcNewName) == 0) {
			iAppendNum++;
			sprintf(pcNewName, "%s %d", pcName, iAppendNum);
			i = -1;
		}
	}
	StructFreeStringSafe(&pDomeIn->display_name);
	pDomeIn->display_name = StructAllocString(pcNewName);
}

static void skyEdRegisterSkyDomes(SkyEditorDoc *pDoc)
{
	int i;
	for ( i=0; i < eaSize(&pDoc->pSkyInfo->skyDomes); i++ ) {
		SkyDome *pDome = pDoc->pSkyInfo->skyDomes[i];
		SkyBlockInfo *pInfo = calloc(1, sizeof(SkyBlockInfo));

		skyEdSetDomeDisplayNameUnique(pDoc, pDome, pDome->display_name);

		pInfo->pDoc = pDoc;
		pInfo->bIsSkyDome = true;
		pInfo->iSkyDomeIdx = i;
		pInfo->pBlockPti = parse_SkyDomeTime;
		pInfo->pcDisplayName = allocAddString(pDome->display_name);
	
		pInfo->pTrack = ui_TimelineTrackCreate(pInfo->pcDisplayName);
		ui_TimelineTrackSetRightClickCallback(pInfo->pTrack, skyEdAddSkyNewSkyBlockTime, pInfo);

		pInfo->pPanel = emPanelCreate("Sky Editor", "Sky Dome Time", 0);
		pInfo->pPane = ui_PaneCreate(0,0,1,1,UIUnitPercentage,UIUnitPercentage,0);
		pInfo->pPane->invisible = true;
		emPanelAddChild(pInfo->pPanel, pInfo->pPane, false);
		pInfo->refreshFunc = skyEdRefreshSkyDomeTimePanel;

		eaPush(&pDoc->ppBlockInfos, pInfo);
	}
}

static SkyBlockInfo* skyEdRegisterSkyBlockInfo(SkyEditorDoc *pDoc, const char *pcDisplayName, const char *pcParseTableName, ParseTable *pBlockPti, skyEdRefreshPanelFunc refreshFunc)
{
	SkyBlockInfo *pInfo = calloc(1, sizeof(SkyBlockInfo));
	pInfo->pDoc = pDoc;
	pInfo->pcDisplayName = pcDisplayName;
	pInfo->pBlockPti = pBlockPti;
	assert(ParserFindColumn(parse_SkyInfo, pcParseTableName, &pInfo->iColumn));
	assert(ParserFindColumn(parse_BlendedSkyInfo, pcParseTableName, &pInfo->iBlendedColumn));

	pInfo->pTrack = ui_TimelineTrackCreate(pcDisplayName);
	ui_TimelineTrackSetRightClickCallback(pInfo->pTrack, skyEdAddSkyNewSkyBlockTime, pInfo);

	pInfo->pPanel = emPanelCreate("Sky Editor", pcDisplayName, 0);
	pInfo->pPane = ui_PaneCreate(0,0,1,1,UIUnitPercentage,UIUnitPercentage,0);
	pInfo->pPane->invisible = true;
	emPanelAddChild(pInfo->pPanel, pInfo->pPane, false);
	pInfo->refreshFunc = refreshFunc;

	eaPush(&pDoc->ppBlockInfos, pInfo);
	return pInfo;
}

static void skyEdInitPanel(const char *pcDisplayName, EMPanel **pPanel, UIPane **pPane)
{
	(*pPanel) = emPanelCreate("Sky Editor", pcDisplayName, 0);
	(*pPane) = ui_PaneCreate(0,0,1,1,UIUnitPercentage,UIUnitPercentage,0);
	(*pPane)->invisible = true;
	emPanelAddChild(*pPanel, *pPane, false);
}

static int skyEdSortBlockInfos(const SkyBlockInfo **left, const SkyBlockInfo **right)
{
	return strcmp((*left)->pcDisplayName, (*right)->pcDisplayName);
}

static void skyEdInitNewDocUI(SkyEditorDoc *pDoc)
{
	SkyBlockInfo *pInfo;

	skyEdInitTimelineUI(pDoc);

	skyEdRegisterSkyBlockInfo(pDoc, "World Lighting",			"SkySun",					parse_SkyTimeSun,				skyEdRefreshWorldLightPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "World Lighting 2",			"SkySecondarySun",			parse_SkyTimeSecondarySun,		skyEdRefreshSecondaryWorldLightPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Character Lighting",		"SkyCharacterLighting",		parse_SkyTimeCharacterLighting,	skyEdRefreshCharLightPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Cloud Shadows",			"SkyCloudShadows",			parse_SkyTimeCloudShadows,		skyEdRefreshCloudShadowPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Shadow Fading",			"SkyShadowFade",			parse_SkyTimeShadowFade,		skyEdRefreshShadowFadePanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Color Correction",			"SkyColorCorrection",		parse_SkyTimeColorCorrection,	skyEdRefreshColorCorrectionPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Outlining",				"SkyOutline",				parse_SkyTimeOutline,			skyEdRefreshOutlinePanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Light Behavior",			"SkyLightBehavior",			parse_SkyTimeLightBehavior,		skyEdRefreshLightBehaviorPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Bloom",					"SkyBloom",					parse_SkyTimeBloom,				skyEdRefreshBloomPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Ambient Occlusion",		"AmbientOcclusion",			parse_SkyTimeAmbientOcclusion,	skyEdRefreshAmbientOcclusionPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Wind",						"Wind",						parse_SkyTimeWind,				skyEdRefreshWindPanel);
	skyEdRegisterSkyBlockInfo(pDoc, "Light Scattering",			"SkyScattering",			parse_SkyTimeScattering,		skyEdRefreshSkyScatteringPanel);

	pInfo = skyEdRegisterSkyBlockInfo(pDoc, "Depth of Field",		"DOFValues",				parse_DOFValues,				skyEdRefreshDOFPanel);
	pInfo->changedFunc = skyEdDOFChangedCB;

	pInfo = skyEdRegisterSkyBlockInfo(pDoc, "Fog",			"SkyFog",					parse_SkyTimeFog,				skyEdRefreshFogPanel);
	pInfo->changedFunc = skyEdFogChangedCB;

	//Leaving this one out for now because color correcting seems to do the same and tinting seams broken
	//skyEdRegisterSkyBlockInfo(pDoc, "Tinting",				"SkyTint",					parse_SkyTimeTint,				skyEdRefreshTintPanel);

	skyEdRegisterSkyBlockInfo(pDoc, "Shadow Rules",				"SkyShadows",				parse_ShadowRules,				skyEdRefreshShadowRulesPanel);
	eaQSort(pDoc->ppBlockInfos, skyEdSortBlockInfos);

	skyEdInitPanel("Sky Dome Common",		&pDoc->UI.pSkyDomeCommonPanel,		&pDoc->UI.pSkyDomeCommonPane);
	skyEdInitPanel("Sky Dome Luminary",		&pDoc->UI.pSkyDomeLuminaryPanel,	&pDoc->UI.pSkyDomeLuminaryPane);
	skyEdInitPanel("Sky Dome Object",		&pDoc->UI.pSkyDomeObjectPanel,		&pDoc->UI.pSkyDomeObjectPane);
	skyEdInitPanel("Sky Dome Star Field",	&pDoc->UI.pSkyDomeStarFieldPanel,	&pDoc->UI.pSkyDomeStarFieldPane);
	skyEdInitPanel("Sky Dome Atmosphere",	&pDoc->UI.pSkyDomeAtmospherePanel,	&pDoc->UI.pSkyDomeAtmospherePane);
	skyEdInitPanel("Sky Dome Lens Flare",	&pDoc->UI.pSkyDomeLensFlarePanel,	&pDoc->UI.pSkyDomeLensFlarePane);
	
	pDoc->UI.pFilePanel = emPanelCreate("Sky Editor", "File", 0);
	pDoc->UI.pFileButton = ui_GimmeButtonCreate(0, 0, GFX_SKY_DICTIONARY, pDoc->emDoc.doc_name, pDoc->pSkyInfo);
	emPanelAddChild(pDoc->UI.pFilePanel, pDoc->UI.pFileButton, true);
	pDoc->UI.pFilenameLabel = ui_LabelCreate(pDoc->emDoc.doc_name, 20, 0);
	emPanelAddChild(pDoc->UI.pFilePanel, pDoc->UI.pFilenameLabel, true);
	ui_WidgetSetWidthEx(UI_WIDGET(pDoc->UI.pFilenameLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->UI.pFilenameLabel), 0, 21, 0, 0);
	eaPush(&pDoc->emDoc.em_panels, pDoc->UI.pFilePanel);

	pDoc->UI.pGeneralPanel = emPanelCreate("Sky Editor", "General", 0);
	pDoc->UI.pGeneralPane = ui_PaneCreate(0,0,1,1,UIUnitPercentage,UIUnitPercentage,0);
	pDoc->UI.pGeneralPane->invisible = true;
	emPanelAddChild(pDoc->UI.pGeneralPanel, pDoc->UI.pGeneralPane, false);
	eaPush(&pDoc->emDoc.em_panels, pDoc->UI.pGeneralPanel);

	skyEdRegisterSkyDomes(pDoc);
	skyEdRefreshUI(pDoc);
}

SkyEditorDoc* skyEdNewDoc(const char *pName)
{
	SkyInfo *pNewSky = StructCreate(parse_SkyInfo);
	SkyEditorDoc *pNewDoc = calloc(1, sizeof(*pNewDoc));
	char pcNewName[256];
	U32 iNewSkyCnt = 1;

	pNewDoc->pSkyInfo = pNewSky;

	if(pName) {
		SkyInfo *pLoadSky = gfxSkyFindSky(pName);
		assert(pLoadSky);
		StructCopyAll(parse_SkyInfo, pLoadSky, pNewSky);
		strcpy(pNewDoc->emDoc.orig_doc_name, pName);

		emDocAssocFile(&pNewDoc->emDoc, pNewDoc->pSkyInfo->filename);
	} else {
		do {
			sprintf(pcNewName, "NewSky_%02d", iNewSkyCnt);
			iNewSkyCnt++;
		} while(gfxSkyFindSky(pcNewName));
		pNewSky->filename_no_path = allocAddString(pcNewName);
		RefSystem_AddReferent(GFX_SKY_DICTIONARY, pcNewName, StructClone(parse_SkyInfo, pNewSky));
		pNewDoc->bNewSky = true;
	}

	// Set up the undo stack
	pNewDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pNewDoc->emDoc.edit_undo_stack, pNewDoc);
	pNewDoc->pNextUndoSkyInfo = StructClone(parse_SkyInfo, pNewDoc->pSkyInfo);
	pNewDoc->pOrigSkyInfo = StructClone(parse_SkyInfo, pNewDoc->pSkyInfo);

	// Init UI
	skyEdInitNewDocUI(pNewDoc);

	return pNewDoc;
}

EMTaskStatus skyEdSaveSky(SkyEditorDoc *pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;

	// Deal with state changes
	if(!emHandleSaveResourceState(pDoc->emDoc.editor, pDoc->emDoc.doc_name, &status))
	{
		// Attempt the save
		SkyInfo *pSkyInfoCopy = StructClone(parse_SkyInfo, pDoc->pSkyInfo);

		// Clone will be freed
		status = emSmartSaveDoc(&pDoc->emDoc, pSkyInfoCopy, pDoc->pOrigSkyInfo, bSaveAsNew);
	}

	if(status == EM_TASK_SUCCEEDED)
	{
		pDoc->bSavingAs = 0;

		// If actually saving, change file association in case it was a rename or a new file
		emDocRemoveAllFiles(&pDoc->emDoc, false);
		emDocAssocFile(&pDoc->emDoc, pDoc->pSkyInfo->filename);
	}

	return status;
}

EMTaskStatus skyEdSaveAsSky(SkyEditorDoc *pDoc)
{
	if(!pDoc->bSavingAs)
	{
		char saveDir[ MAX_PATH ];
		char saveFile[ MAX_PATH ];
		char nameBuf[ MAX_PATH ];
		if( UIOk != ui_ModalFileBrowser( "Save Sky As", "Save As", UIBrowseNewNoOverwrite, UIBrowseFiles, false, 
			"environment/skies", "environment/skies", ".Sky", SAFESTR( saveDir ), SAFESTR( saveFile ), pDoc->emDoc.doc_name))
		{
				return EM_TASK_FAILED;
		}
		getFileNameNoExt( saveFile, saveFile );

		if(emGetEditorDoc(saveFile, "Sky") || resGetInfo(GFX_SKY_DICTIONARY, saveFile))
			emMakeUniqueDocName(&pDoc->emDoc, saveFile, "Sky", GFX_SKY_DICTIONARY);
		else
			strcpy(pDoc->emDoc.doc_name, saveFile);

		strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
		pDoc->emDoc.name_changed = 1;
		pDoc->emDoc.saved = 0;

		sprintf(nameBuf, "%s/%s.Sky", saveDir, pDoc->emDoc.doc_name);
		pDoc->pSkyInfo->filename = allocAddFilename(nameBuf);

		pDoc->bSavingAs = true;
	}

	return skyEdSaveSky(pDoc, true);
}

static void skyEdViewTypeChangedCB(UIComboBox *pCombo, int val, void *unused)
{
	SkyEditorDoc* pDoc = (SkyEditorDoc*)emGetActiveEditorDoc();
	g_SkyViewType = val;
	skyEdSetSkyOverrides(pDoc);
}

void skyEdInit(EMEditor *pEditor)
{
	EMToolbar *pToolbar;
	UIComboBox *pComboBox;
	UILabel *pLabel;
	
	// Have Editor Manager handle a lot of change tracking
	emAutoHandleDictionaryStateChange(pEditor, GFX_SKY_DICTIONARY, false, NULL, NULL, NULL, NULL, NULL);

	eaPush( &pEditor->toolbars, emToolbarCreateDefaultFileToolbar() );
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	pToolbar = emToolbarCreate( 0 );
	eaPush( &pEditor->toolbars, pToolbar );

	pLabel = ui_LabelCreate("Viewing: ", 1, 0);
	emToolbarAddChild(pToolbar, pLabel, true);

	pComboBox = ui_ComboBoxCreateWithEnum(60, 1, 150, SkyViewTypeEnum, skyEdViewTypeChangedCB, NULL);
	ui_ComboBoxSetSelectedEnum(pComboBox, 0);
	ui_WidgetSetHeight(UI_WIDGET(pComboBox), 20);
	emToolbarAddChild(pToolbar, pComboBox, true);
}

void skyEdCloseSky(SkyEditorDoc *pDoc)
{
}

void skyEdReloadSky(SkyEditorDoc *pDoc)
{
	SkyInfo *pSkyInfo = NULL;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	// Clear the undo stack on revert
	EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
	StructDestroySafe(parse_SkyInfo, &pDoc->pNextUndoSkyInfo);
	StructDestroySafe(parse_SkyInfo, &pDoc->pOrigSkyInfo);

	pSkyInfo = RefSystem_ReferentFromString(GFX_SKY_DICTIONARY, pDoc->emDoc.orig_doc_name);
	if (pSkyInfo) {
		StructCopyAll(parse_SkyInfo, pSkyInfo, pDoc->pSkyInfo);
		pDoc->pNextUndoSkyInfo = StructClone(parse_SkyInfo, pDoc->pSkyInfo);
		pDoc->pOrigSkyInfo = StructClone(parse_SkyInfo, pDoc->pSkyInfo);
	}

	skyEdRefreshUI(pDoc);
}

void skyEdDraw(SkyEditorDoc *pDoc)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	F32 pCurTime = SE_TrackTimeToSky(ui_TimelineGetTime(pDoc->UI.pTimeline));

	pCurTime += gfx_state.time_rate * gfx_state.frame_time * pDoc->fPlaySpeed;
	while(pCurTime > 24.0f)
		pCurTime -= 24.0f;

	pCamera->time_override = pCurTime;
	ui_TimelineSetTime(pDoc->UI.pTimeline, SE_SkyTimeToTrack(pCurTime));
}

void skyEdDrawGhosts(SkyEditorDoc *pDoc)
{
	Vec3 vMin = {-30000, -1, -30000};
	Vec3 vMax = { 30000,  1,  30000};
	Mat4 mMat;
	copyMat4(unitmat, mMat);

	if(pDoc->UI.iFogHeightFade > 0) {
		Color cColor;
		setVec4(cColor.rgba, 255, 255, 255, pDoc->UI.iFogHeightFade);
		mMat[3][1] = pDoc->UI.fFogHeight;
		gfxDrawBox3D(vMin, vMax, mMat, cColor, 0);
		pDoc->UI.iFogHeightFade-=5;
	}
}

void skyEdInitGraphics(void)
{
}

void skyEdGotFocus(SkyEditorDoc *pDoc)
{
	GfxCameraController *pCamera = emGetWorldCamera();
	pCamera->override_time = 1;
	ui_WidgetAddToDevice( UI_WIDGET( pDoc->UI.pTimelinePane ), NULL );
	skyEdRefreshUI(pDoc);
	skyEdSetSkyOverrides(pDoc);
}

void skyEdLostFocus(SkyEditorDoc *pDoc)
{
	GfxCameraController *pCamera = emGetWorldCamera();
	pCamera->override_time = 0;
	ui_WidgetRemoveFromGroup( UI_WIDGET( pDoc->UI.pTimelinePane ));
	gfxCameraControllerSetSkyGroupOverride(pCamera, NULL, NULL);
	StructCopyAll(parse_SkyInfo, pDoc->pOrigSkyInfo, gfxSkyFindSky(pDoc->pSkyInfo->filename_no_path));
	gfxSkyClearAllVisibleSkies();
}
