/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "enclayereditor.h"
#include "eventlogviewer.h"
#include "oldencounter_common.h"
#include "WorldEditor.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#endif

AUTO_COMMAND ACMD_NAME("ELE.ToggleAggroRadius");
void ELEToggleAggroDisplayMenuCB(void)
{
#ifndef NO_EDITORS
	g_ShowAggroRadius = !g_ShowAggroRadius;
#endif
}

AUTO_COMMAND ACMD_NAME("ELE.LayerCut");
void ELELayerCutMenuCB(void)
{
#ifndef NO_EDITORS
	ELECopySelectedToClipboard(GEGetActiveEditorDocEM("encounterlayer"), false);
#endif
}

AUTO_COMMAND ACMD_NAME("ELE.LayerPaste");
void ELELayerPasteMenuCB(void)
{
#ifndef NO_EDITORS
	ELEPasteSelectedFromClipboard(GEGetActiveEditorDocEM("encounterlayer"));
#endif
}

AUTO_COMMAND ACMD_NAME("ELE.SaveEncAsDef");
void ELESaveSelectedEncAsDefMenuCB(void)
{
#ifndef NO_EDITORS
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
	GEPlacementTool* placementTool = &encLayerDoc->placementTool;
	EncounterLayer* encLayer = encLayerDoc->layerDef;
	if (!GEPlacementToolIsInPlacementMode(placementTool))
	{
		int* selEncList = ELEGetSelectedEncounterIndexList(encLayerDoc);
		OldStaticEncounter* staticEnc = NULL;

		if(eaiSize(&selEncList) != 1)
		{
			Errorf("Must have exactly one encounter selected to save as def");
			return;
		}

		staticEnc = encLayer->staticEncounters[selEncList[0]];

		ELESaveEncAsDef(encLayerDoc, staticEnc);
	}
#endif
}

AUTO_COMMAND ACMD_NAME("DisableQuickPlace") ACMD_CMDLINE;
void ELEDisableQuickPlace(int value)
{
#ifndef NO_EDITORS
	g_DisableQuickPlace = value;
#endif
}

AUTO_COMMAND ACMD_NAME("ELE.SetTeamSize");
void ELEForceTeamSizeMenuCB(void)
{
#ifndef NO_EDITORS
	int currHeight = 0;
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*)GEGetActiveEditorDocEM("encounterlayer");
	EncounterLayer* layer = encLayerDoc->layerDef;
	UILabel* sliderDescLabel;
	UISlider* teamSizeSlider;
	char tmpStr[1024];

	// Create the modal window for the team size override
	encLayerDoc->uiInfo.forceTeamSizeWindow = ui_WindowCreate("Layer Team Size Override", g_ui_State.mouseX, g_ui_State.mouseY, 260, 500);
	ui_WindowSetModal(encLayerDoc->uiInfo.forceTeamSizeWindow, true);
	ui_WindowShow(encLayerDoc->uiInfo.forceTeamSizeWindow);

	// Create a team size slider for the layer's team size override
	sliderDescLabel = ui_LabelCreate("Team Size", 0, currHeight);
	teamSizeSlider = ui_IntSliderCreate(sliderDescLabel->widget.width + UI_HSTEP, currHeight, 100, 0, MAX_TEAM_SIZE, layer->forceTeamSize);
	ui_SliderSetPolicy(teamSizeSlider, UISliderContinuous);
	if (layer->forceTeamSize)
		sprintf(tmpStr, "%i", layer->forceTeamSize);
	else
		strcpy(tmpStr, "No Override");
	encLayerDoc->uiInfo.teamSizeLabel = ui_LabelCreate(tmpStr, teamSizeSlider->widget.x + teamSizeSlider->widget.width + GE_SPACE, 0);
	ui_SliderSetChangedCallback(teamSizeSlider, ELEForceTeamSizeChanged, encLayerDoc);
	ui_WindowAddChild(encLayerDoc->uiInfo.forceTeamSizeWindow, sliderDescLabel);
	ui_WindowAddChild(encLayerDoc->uiInfo.forceTeamSizeWindow, teamSizeSlider);
	ui_WindowAddChild(encLayerDoc->uiInfo.forceTeamSizeWindow, encLayerDoc->uiInfo.teamSizeLabel);
	currHeight += teamSizeSlider->widget.height;

	encLayerDoc->uiInfo.forceTeamSizeWindow->widget.height = currHeight;
#endif
}

AUTO_COMMAND ACMD_NAME("ELE.OpenLogViewer");
void ELEOpenLogViewerCB(void)
{
#ifndef NO_EDITORS
	eventlogviewer_Create();
#endif
}

AUTO_RUN;
void EncounterSystemEditorAutoRunInit(void)
{
#ifndef NO_EDITORS
	oldencounter_RegisterLayerChangeCallbacks(NULL, ELEEncounterMapUnload);
#endif
}

