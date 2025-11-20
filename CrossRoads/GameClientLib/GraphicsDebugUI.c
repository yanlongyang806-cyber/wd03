#include "GraphicsDebugUI.h"
#include "UILib.h"
#include "Prefs.h"
#include "RdrShader.h"
#include "GfxDebug.h"
#include "cmdparse.h"
#include "qsortG.h"
#include "wlTime.h"
#include "GfxEditorIncludes.h"
#include "earray.h"
#include "utils.h"
#include "RdrDrawList.h"
#include "TextBuffer.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef UITab* (*TabCreateFunction)();

typedef struct GraphicsDebugTab
{
	UITab *tab;
	TabCreateFunction create_func;
} GraphicsDebugTab;

typedef struct GraphicsDebugData
{
	UIWindow *window;
	UITabGroup *tabs;
	UILabel *performance_summary_label;
	GraphicsDebugTab **tab_infos;
	const char *selectedVisualization;
	const char *selectedShaderTestCommon;
	const char *selectedShaderTestAll;
} GraphicsDebugData;

GraphicsDebugData gfx_debug_data;

static const char * const g_none_str = "(none)";

static void selectedShaderTestCommonCB(UIAnyWidget *widget, UserData unused)
{
	UIComboBox *combo = (UIComboBox*)widget;
	const char *selected = ui_ComboBoxGetSelectedObject(combo);
	globCmdParsef("ShaderTest \"%s\"", (selected && (selected != g_none_str))?selected:"");
	gfx_debug_data.selectedShaderTestCommon = selected;
}

static void selectedShaderTestAllCB(UIAnyWidget *widget, UserData unused)
{
	UIComboBox *combo = (UIComboBox*)widget;
	const char *selected = ui_ComboBoxGetSelectedObject(combo);
	globCmdParsef("ShaderTest \"%s\"", (selected && (selected != g_none_str))?selected:"");
	gfx_debug_data.selectedShaderTestAll = selected;
}

static void setTimeStepScale(UIAnyWidget *widget, UserData userData)
{
	intptr_t scale = (intptr_t)userData;
	wlTimeSetStepScaleDebug((F32)scale);
}

static void toggleShaderTest(UIAnyWidget *widget, UserData userData)
{
	UIRadioButton *radio = (UIRadioButton *)widget;
	const char *var = userData;
	if (ui_RadioButtonGroupGetActive(radio->group) == radio) {
		if (!rdrShaderGetTestDefine(0) || stricmp(rdrShaderGetTestDefine(0), var)!=0) {
			globCmdParsef("ShaderTest %s", var);
		}
	} else {
		if (stricmp(rdrShaderGetTestDefine(0), var)==0) {
			globCmdParse("ShaderTest \"\"");
		}
	}
}

static void toggleCmdParseRadio(UIAnyWidget *widget, UserData userData)
{
	UIRadioButton *radio = (UIRadioButton *)widget;
	const char *var = userData;
	char *ret=NULL;
	int curVal=0;
	int desiredVal=0;
	if (0==globCmdParseAndReturn(var, &ret, 0, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL)) {
		if (strStartsWith(ret, var)) {
			sscanf(ret + strlen(var)+1, "%d", &curVal);
		} else {
			Errorf("cmdParse failed on string \"%s\"", var);
		}
	} else {
		Errorf("cmdParse failed on string \"%s\" (or command is not bound to a variable?)", var);
	}
	desiredVal = ui_RadioButtonGroupGetActive(radio->group) == radio;
	if (curVal != desiredVal) {
		if (0==globCmdParsef("%s %d", var, desiredVal)) {
			Errorf("cmdParse failed on string \"%s %d\"", var, desiredVal);
		}
	}
}

static int cmdParseGetCurrentValue(const char *var)
{
	char *ret=NULL;
	int curVal=0;
	if (0==globCmdParseAndReturn(var, &ret, 0, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL)) {
		if (strStartsWith(ret, var)) {
			char *value = ret + strlen(var);
			while (*value==' ')
				value++;
			if (value[0]=='\"') {
				if (value[1]=='\"')
					curVal = 0;
				else
					curVal = 1;
			} else {
				sscanf(value, "%d", &curVal);
			}
		} else {
			Errorf("cmdParse failed on string \"%s\"", var);
		}
	} else {
		Errorf("cmdParse failed on string \"%s\" (or command is not bound to a variable?)", var);
	}
	estrDestroy(&ret);
	return curVal;
}

static void toggleCmdParseCheck(UIAnyWidget *widget, UserData userData)
{
	UICheckButton *check = (UICheckButton *)widget;
	const char *var = userData;
	int curVal=cmdParseGetCurrentValue(var);
	int desiredVal=ui_CheckButtonGetState(check);
	if (curVal != desiredVal) {
		if (0==globCmdParsef("%s %d", var, desiredVal)) {
			Errorf("cmdParse failed on string \"%s %d\"", var, desiredVal);
		}
	}
}

#define LINE_HEIGHT 16
#define LINE_INDENT 5

static F32 addCmdParseCheck(UIScrollArea *scrollArea, const char *title, const char *command, F32 x, F32 y)
{
	UICheckButton *check = ui_CheckButtonCreate(x, y, title, !!cmdParseGetCurrentValue(command));
	ui_CheckButtonSetToggledCallback(check, toggleCmdParseCheck, (UserData)command);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(check));
	y+=LINE_HEIGHT;
	return y;
}

static F32 addCmdParseButton(UIScrollArea *scrollArea, const char *title, const char *command, F32 x, F32 y)
{
	UIButton *button = ui_ButtonCreate(title, x, y, NULL, NULL);
	ui_ButtonSetCommand(button, command);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(button));
	y+=LINE_HEIGHT+UI_HSTEP;
	return y;
}

static void onHDRSlopeScaleDepthBiasSliderChangeFunc(UIAnyWidget *slider, bool finished, UserData userdata)
{
	char buf[128];
	F32 bias = ui_FloatSliderGetValue(slider);
	sprintf(buf, "%.7f", bias);
	ui_TextEntrySetText((UITextEntry*)userdata, buf);

	rdrSetHDRSlopeScaleDepthBias(bias);
}

static void onHDRDepthBiasSliderChangeFunc(UIAnyWidget *slider, bool finished, UserData userdata)
{
	char buf[128];
	F32 bias = ui_FloatSliderGetValue(slider);
	sprintf(buf, "%.7f", bias);
	ui_TextEntrySetText((UITextEntry*)userdata, buf);

	rdrSetHDRDepthBias(bias);
}

static void onHDRFloatSliderTextChanged(UITextEntry *textentry, UISlider *slider)
{
	ui_FloatSliderSetValueAndCallback(slider, atof(ui_TextEntryGetText(textentry)));
}


static const int HDR_DEPTHBIAS_FLOAT_EDIT_WIDTH = 200;

static F32 addHDRSliders(UIScrollArea *scrollArea, const char *title, F32 x, F32 y)
{
	UISlider *slider;
	UITextEntry * textentry;
	char buf[128];
	int is_d3d11 = gfx_state.settings.device_type && !stricmp(gfx_state.settings.device_type, "Direct3D11");


	textentry = ui_TextEntryCreate("", x, y);
	ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate("HDR Slopescale Depth Bias", x + HDR_DEPTHBIAS_FLOAT_EDIT_WIDTH + UI_HSTEP, y));
	y+=LINE_HEIGHT+UI_HSTEP;

	slider = ui_FloatSliderCreate(x, y, 1000, -1.0f, 0.001f, rdrGetHDRSlopeScaleDepthBias());
	y+=LINE_HEIGHT+UI_HSTEP;

	ui_TextEntrySetFinishedCallback(textentry, onHDRFloatSliderTextChanged, slider);
	textentry->widget.height = 15;
	textentry->widget.width = HDR_DEPTHBIAS_FLOAT_EDIT_WIDTH;
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(textentry));
	sprintf(buf, "%.2f", rdrGetHDRSlopeScaleDepthBias());
	ui_TextEntrySetText(textentry, buf);

	ui_SliderSetPolicy(slider, UISliderContinuous);
	ui_SliderSetChangedCallback(slider, onHDRSlopeScaleDepthBiasSliderChangeFunc, textentry);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(slider));


	textentry = ui_TextEntryCreate("", x, y);
	ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate("HDR Depth Bias", x + HDR_DEPTHBIAS_FLOAT_EDIT_WIDTH + UI_HSTEP, y));
	y+=LINE_HEIGHT+UI_HSTEP;
	slider = ui_FloatSliderCreate(x, y, 1000, is_d3d11 ? -256 : -0.001f, is_d3d11 ? 256 : 0.001f, rdrGetHDRDepthBias());
	y+=LINE_HEIGHT+UI_HSTEP;

	ui_TextEntrySetFinishedCallback(textentry, onHDRFloatSliderTextChanged, slider);
	textentry->widget.height = 15;
	textentry->widget.width = 200;
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(textentry));
	sprintf(buf, "%.2f", rdrGetHDRDepthBias());
	ui_TextEntrySetText(textentry, buf);

	ui_SliderSetPolicy(slider, UISliderContinuous);
	ui_SliderSetChangedCallback(slider, onHDRDepthBiasSliderChangeFunc, textentry);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(slider));

	return y;
}

static void onPerfModelLODOffsetSliderChangeFunc(UIAnyWidget *slider, bool finished, UserData userdata)
{
	char buf[128];
	int lod_offset = ui_IntSliderGetValue(slider);
	sprintf(buf, "%d", lod_offset);
	ui_TextEntrySetText((UITextEntry*)userdata, buf);

	gfxModelSetLODOffset(lod_offset);
}

static void onPerfModelLODSliderTextChanged(UITextEntry *textentry, UISlider *slider)
{
	ui_IntSliderSetValueAndCallback(slider, atoi(ui_TextEntryGetText(textentry)));
}

static void onPerfModelLODForceSliderChangeFunc(UIAnyWidget *slider, bool finished, UserData userdata)
{
	char buf[128];
	int lod_force = ui_IntSliderGetValue(slider);
	sprintf(buf, "%d", lod_force);
	ui_TextEntrySetText((UITextEntry*)userdata, buf);

	gfxModelSetLODForce(lod_force);
}

static F32 addIntEditSlider(UIScrollArea *scrollArea, const char *title, F32 x, F32 y, 
	F32 edit_width, F32 slider_width, int slider_min, int slider_max, int slider_initial_value,
	UIActivationFunc onEditText, UISliderChangeFunc onSlider)
{
	UISlider *slider;
	UITextEntry * textentry;
	char buf[128];

	textentry = ui_TextEntryCreate("", x, y);
	ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate(title, x + edit_width + UI_HSTEP, y));
	y+=LINE_HEIGHT+UI_HSTEP;

	slider = ui_IntSliderCreate(x, y, slider_width, slider_min, slider_max, slider_initial_value);
	y+=LINE_HEIGHT+UI_HSTEP;

	
	ui_TextEntrySetFinishedCallback(textentry, onEditText, slider);
	textentry->widget.height = 15;
	textentry->widget.width = edit_width;
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(textentry));
	sprintf(buf, "%d", slider_initial_value);
	ui_TextEntrySetText(textentry, buf);

	ui_SliderSetPolicy(slider, UISliderDiscrete);
	ui_SliderSetChangedCallback(slider, onSlider, textentry);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(slider));

	return y;
}

static F32 addPerfModelLODSliders(UIScrollArea *scrollArea, const char *title, F32 x, F32 y)
{
	const int MODELLODOFFSET_INT_EDIT_WIDTH = 60;
	const int SLIDER_WIDTH = 100;
	const int MIN_LOD_OFFSET = -4;
	const int MAX_LOD_OFFSET = 4;
	y = addIntEditSlider(scrollArea, "Model LOD Offset", x, y, 
		MODELLODOFFSET_INT_EDIT_WIDTH, SLIDER_WIDTH, MIN_LOD_OFFSET, MAX_LOD_OFFSET, gfxModelGetLODOffset(),
		onPerfModelLODSliderTextChanged, onPerfModelLODOffsetSliderChangeFunc);

	y = addIntEditSlider(scrollArea, "Force Model LOD", x, y, 
		MODELLODOFFSET_INT_EDIT_WIDTH, SLIDER_WIDTH, -1, MAX_LOD_OFFSET, gfxModelGetLODForce(),
		onPerfModelLODSliderTextChanged, onPerfModelLODForceSliderChangeFunc);
	return y;
}


static UITab* gfxDebugAddGeneralTab(void)
{
	UITab *pTab = ui_TabCreate("General");
	F32 x=0, y=0;
	UIScrollArea *scrollArea;
	UIButton *button;
	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	// Pause/play/ff
	button = ui_ButtonCreateImageOnly("play_icon", x, y, setTimeStepScale, (UserData)1 );
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(button));

	button = ui_ButtonCreateImageOnly("pause_icon",
		UI_WIDGET( button )->x + UI_WIDGET( button )->width + 4,
		y, setTimeStepScale, (UserData)0 );
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(button));

	button = ui_ButtonCreateImageOnly("ff_icon",
		UI_WIDGET( button )->x + UI_WIDGET( button )->width + 4,
		y, setTimeStepScale, (UserData)2 );
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(button));
	y += UI_WIDGET(button)->height + 2;

	y = addCmdParseCheck(scrollArea, "Disable Regions", "drawAllRegions", x, y);

	y = addCmdParseCheck(scrollArea, "Show Time", "showtime", x, y);
	y = addCmdParseCheck(scrollArea, "Show Memory Usage", "showmem", x, y);
	y = addCmdParseCheck(scrollArea, "Show FPS", "showfps", x, y);
	y = addCmdParseCheck(scrollArea, "FPS Graph", "fpsgraph", x, y);

	ui_ScrollAreaSetSize(scrollArea, 1000, y);
	return pTab;
}

static void selectedDrawStaticCollisionCB(UIAnyWidget *widget, UserData unused)
{
	UIComboBox *combo = (UIComboBox*)widget;
	gfx_state.debugDrawStaticCollision = ui_ComboBoxGetSelected(combo);
}

static UITab* gfxDebugAddObjectsTab(void)
{
	UITab *pTab = ui_TabCreate("Objects");
	F32 x=0, y=0;
	UIScrollArea *scrollArea;
	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	// Checkboxes to toggle drawing normal, skinned, terrain, instanced, trees, etc
	ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate("Enable Drawing (will still be updated):", x, y));
	y+=LINE_HEIGHT;
	x+=LINE_INDENT;

	y = addCmdParseCheck(scrollArea, "Normal Objects", "dbgShowNormal", x, y);
	y = addCmdParseCheck(scrollArea, "Clusters", "debug_cluster_show_clusters", x + LINE_INDENT, y);
	y = addCmdParseCheck(scrollArea, "Drawable Entries", "debug_cluster_show_nonclusters", x + LINE_INDENT, y);

	y = addCmdParseCheck(scrollArea, "Skinned Objects", "dbgShowSkinned", x, y);
	y = addCmdParseCheck(scrollArea, "Terrain", "dbgShowTerrain", x, y);
	y = addCmdParseCheck(scrollArea, "Primitives (includes Thumbnails)", "dbgShowPrimitive", x, y);

	y += 4;

	{
		static const char **ppchOptions;
		UIComboBox *comboBox;
		ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate("Static Collision Draw", x, y));
		if (!ppchOptions) {
			int i;
			static const char *options_src[] = {
				"Off",
				"By Type",
				"By Density",
				"Beaconize",
				"Playable Volume Geo"
			};
			for (i=0; i<ARRAY_SIZE(options_src); i++) 
				eaPush(&ppchOptions, options_src[i]);
		}
		comboBox = ui_ComboBoxCreate(x+140, y, 100, NULL, (cUIModel)&ppchOptions, NULL);
		ui_ComboBoxSetSelected(comboBox, gfx_state.debugDrawStaticCollision);
		ui_ComboBoxSetSelectedCallback(comboBox, selectedDrawStaticCollisionCB, 0);
		ui_WidgetSetWidthEx(UI_WIDGET(comboBox), 1.f, UIUnitPercentage);
		ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(comboBox));
		y+=LINE_HEIGHT+UI_STEP;
	}
	y = addCmdParseCheck(scrollArea, "Draw Dynamic Collision", "dbgShowDynamicCollision", x, y);  // this is actually in GfxDebug.c
	y = addCmdParseCheck(scrollArea, "Draw Movement Capsules", "mmDrawCaps", x, y);  // this is actually in GfxDebug.c

	x-=LINE_INDENT;
	y+=LINE_HEIGHT;

	y = addCmdParseCheck(scrollArea, "Disable World Animations", "disableWorldAnimation", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Wind", "disableWind", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Wind Grid", "dfxDrawWindGrid", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Occlusion", "noZocclusion", x, y);

	y+=LINE_HEIGHT;

	y = addCmdParseCheck(scrollArea, "Show clustering on locked layers", "gfxShowClustering", x, y);
	y = addCmdParseCheck(scrollArea, "Show WorldCells", "worldCellVisualize3D", x, y);
	y = addCmdParseCheck(scrollArea, "Show clusters with wireframe", "debug_cluster_wireframe", x, y);

	ui_ScrollAreaSetSize(scrollArea, 1000, y);
	return pTab;
}

static UITab* gfxDebugAddMaterialsTab(void)
{
	UITab *pTab = ui_TabCreate("Materials");
	F32 x=0, y=0;
	UIScrollArea *scrollArea;
	UIComboBox *comboBox;
	static const char **commonOptions;
	static const char **allOptions;
	int idx;
	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	// ShaderTest dropdown with common ones (show normals, etc)
	ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate("ShaderTest (Common)", x, y));
	if (!commonOptions) {
		int i;
		static const char *options_src[] = {
			"ShowAlbedo",
			"ShowNormals",
			"ShowVertexNormals",
			"ShowVertexBinormals",
			"ShowVertexTangents",
			"ShowColor0",
			"ShowTangentNormals",
			"ShowVSNormals",
			"ShowVSVertexNormals",
			"ShowTexcoord0",
			"ShowVertexLighting",
			"ShowObjectsWithWind",
		};
		eaPush(&commonOptions, g_none_str);
		for (i=0; i<ARRAY_SIZE(options_src); i++) 
			eaPush(&commonOptions, options_src[i]);
	}
	comboBox = ui_ComboBoxCreate(x+140, y, 100, NULL, (cUIModel)&commonOptions, NULL);
	idx = eaFind(&commonOptions, gfx_debug_data.selectedShaderTestCommon);
	ui_ComboBoxSetSelected(comboBox, MAX(0, idx));
	ui_ComboBoxSetSelectedCallback(comboBox, selectedShaderTestCommonCB, 0);
	ui_WidgetSetWidthEx(UI_WIDGET(comboBox), 1.f, UIUnitPercentage);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(comboBox));
	y+=LINE_HEIGHT+UI_STEP;

	// ShaderTest dropdown with all
	ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate("ShaderTest (All)", x, y));
	if (!allOptions) {
		StashTableIterator iter;
		StashElement elem;
		stashGetIterator(g_all_shader_defines, &iter);
		while (stashGetNextElement(&iter, &elem)) {
			eaPush(&allOptions, stashElementGetStringKey(elem));
		}
		eaPush(&allOptions, g_none_str);
		eaQSort(allOptions, strCmp);
	}
	comboBox = ui_ComboBoxCreate(x+140, y, 100, NULL, (cUIModel)&allOptions, NULL);
	idx = eaFind(&allOptions, gfx_debug_data.selectedShaderTestAll);
	ui_ComboBoxSetSelected(comboBox, MAX(0, idx));
	ui_ComboBoxSetSelectedCallback(comboBox, selectedShaderTestAllCB, (void*)1);
	ui_WidgetSetWidthEx(UI_WIDGET(comboBox), 1.f, UIUnitPercentage);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(comboBox));
	y+=LINE_HEIGHT+UI_STEP;

	y = addCmdParseCheck(scrollArea, "No Exposure", "noexposure", x, y);
	y = addCmdParseCheck(scrollArea, "White Textures", "whiteTextures", x, y);
	y = addCmdParseCheck(scrollArea, "Simple Materials", "simpleMaterials", x, y);

	ui_ScrollAreaSetSize(scrollArea, 1000, y);
	return pTab;
}

static UITab* gfxDebugAddLightingTab(void)
{
	UITab *pTab = ui_TabCreate("Lighting");
	F32 x=0, y=0;
	UIScrollArea *scrollArea;
	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	y = addCmdParseButton(scrollArea, "Light Debugger", "lightDebugger", x, y);
	y+=UI_HSTEP;

	y = addCmdParseCheck(scrollArea, "Unlit", "unlit", x, y);

	y = addHDRSliders(scrollArea, "Depth biases", x, y);

	ui_ScrollAreaSetSize(scrollArea, 1000, y);
	return pTab;
}

static UITab* gfxDebugAddFXTab(void)
{
	UITab *pTab = ui_TabCreate("FX");
	F32 x=0, y=0;
	UIScrollArea *scrollArea;
	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	y = addCmdParseButton(scrollArea, "FX Debugger", "dfxDebug", x, y);
	y+=UI_HSTEP;
	y = addCmdParseButton(scrollArea, "FX Log", "dfxLoggerToggle", x, y);
	y+=UI_HSTEP;

	y = addCmdParseCheck(scrollArea, "Fx Count", "dfxShowCount", x, y);
	y = addCmdParseCheck(scrollArea, "No FX", "dfxOff", x, y);
	y = addCmdParseCheck(scrollArea, "Fx Pause", "dfxPauseAll", x, y);
	y = addCmdParseCheck(scrollArea, "FX No Wireframe", "dfxNoWireFrame", x, y);
	y = addCmdParseCheck(scrollArea, "FP Overflow Error", "dfxFPErrorOnOverflow", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Wind Grid", "dfxDrawWindGrid", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Splat Collision", "dfxDrawSplatCollision", x, y);
	y = addCmdParseCheck(scrollArea, "Draw FX Sky Volumes", "dfxDrawSkyVolumes", x, y);
	y = addCmdParseCheck(scrollArea, "Draw FX Rays", "dfxDrawRays", x, y);
	y = addCmdParseCheck(scrollArea, "Draw FX Transforms", "dfxDrawFxTransforms", x, y);
	y = addCmdParseCheck(scrollArea, "Draw FX Visibility", "dfxDrawFxVisibility", x, y);
	y = addCmdParseCheck(scrollArea, "Draw FX Physics", "dfxDrawPhysics", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Meshtrail Debug", "dfxDrawMeshTrailDebug", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Impact Triggers", "dfxDrawImpactTriggers", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Debug FX labels", "dfxDrawLabelsForDebug", x, y);
	y = addCmdParseCheck(scrollArea, "Draw All FX labels", "dfxDrawLabelsForAllFX", x, y);

	ui_ScrollAreaSetSize(scrollArea, 1000, y);
	return pTab;
}

static UITab* gfxDebugAddSkyTab(void)
{
	UITab *pTab = ui_TabCreate("Sky");
	F32 x=0, y=0;
	UIScrollArea *scrollArea;
	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	y = addCmdParseCheck(scrollArea, "No Sky", "noSky", x, y);
	y = addCmdParseCheck(scrollArea, "No Fog", "noFog", x, y);
	y = addCmdParseCheck(scrollArea, "No Volume Skies", "disable_sky_volumes", x, y);

	ui_ScrollAreaSetSize(scrollArea, 1000, y);
	return pTab;	
}

static UITab* gfxDebugAddPostProcessingTab(void)
{
	UITab *pTab = ui_TabCreate("Post Processing");
	F32 x=0, y=0;
	UIScrollArea *scrollArea;
	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	y = addCmdParseCheck(scrollArea, "Surface Debug", "surface_debug", x, y);
	y = addCmdParseCheck(scrollArea, "Thumbnails: Shadow Buffer Debug", "shadow_buffer_debug", x, y);
	y = addCmdParseCheck(scrollArea, "Thumbnails: Z-Prepass Debug", "zprepass_debug", x, y);
	y = addCmdParseCheck(scrollArea, "Thumbnails: Postprocessing Debug", "postProcessingDebug", x, y);
	y = addCmdParseCheck(scrollArea, "Thumbnails: Postprocessing LUT Debug", "postProcessingLUTDebug", x, y);
	y = addCmdParseCheck(scrollArea, "Thumbnails: Bloom Debug", "bloomDebug", x, y);
	y = addCmdParseCheck(scrollArea, "Thumbnails: Low-res Alpha Debug", "debug_low_res_alpha", x, y);
	y = addCmdParseCheck(scrollArea, "No Exposure", "noExposure", x, y);

	ui_ScrollAreaSetSize(scrollArea, 1000, y);
	return pTab;	
}

static void gfxDebugVisualizationSetSelected(UIRadioButtonGroup *radioGroup)
{
	int i;
	for ( i=0; i < eaSize(&radioGroup->buttons); i++ ) {
		if(radioGroup->buttons[i]->toggledData == gfx_debug_data.selectedVisualization) {
			ui_RadioButtonGroupSetActive(radioGroup, radioGroup->buttons[i]);
			break;
		}
	}
}

static void gfxDebugVisualizationRadioGroupCB(UIRadioButtonGroup *radioGroup, UserData unused)
{
	UIRadioButton *button = ui_RadioButtonGroupGetActive(radioGroup);
	if(!button)
		return;
	gfx_debug_data.selectedVisualization = button->toggledData;
}

static UITab* gfxDebugAddPerformanceTab(void)
{
	UITab *pTab = ui_TabCreate("Performance");
	F32 x=0, y=0;
	UIRadioButtonGroup *radioGroup;
	UIRadioButton *radio;
	UIScrollArea *scrollArea;
	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	gfx_debug_data.performance_summary_label = ui_LabelCreate("Bottleneck: TBD", x, y);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(gfx_debug_data.performance_summary_label));
	y+=LINE_HEIGHT;

	ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate("Debug Visualizers:", x, y));
	y+=LINE_HEIGHT;

	// Radio group toggling what visualization mode is on
	radioGroup = ui_RadioButtonGroupCreate();
	ui_RadioButtonGroupSetToggledCallback(radioGroup, gfxDebugVisualizationRadioGroupCB, NULL);
	x+=LINE_INDENT;

	radio = ui_RadioButtonCreate(x, y, "None", radioGroup);
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(radio)); y+=LINE_HEIGHT;
	ui_RadioButtonGroupSetActive(radioGroup, radio);

#define ADDIT(title, func, param)							\
	radio = ui_RadioButtonCreate(x, y, title, radioGroup);	\
	ui_ScrollAreaAddChild(scrollArea, UI_WIDGET(radio)); y+=LINE_HEIGHT;		\
	ui_RadioButtonSetToggledCallback(radio, func, param);
	
	ADDIT("Show Material Cost", toggleCmdParseRadio, "showMaterialCost");
	// Add Show Material Cost Dropdown?

	ADDIT("Show Alpha", toggleCmdParseRadio, "show_alpha");
	ADDIT("Show Double Sided", toggleCmdParseRadio, "show_double_sided");
	ADDIT("Show Cubemap", toggleShaderTest, "ShowCubeMap");
	ADDIT("Show Refract", toggleShaderTest, "ShowRefract");
	ADDIT("Show HasNormalMap", toggleShaderTest, "ShowHasNormalMap");
	ADDIT("Show Instanced", toggleCmdParseRadio, "show_instanced_objects");
#undef ADDIT
	gfxDebugVisualizationSetSelected(radioGroup);

	x-=LINE_INDENT;

	y+=UI_HSTEP;

	// Checkboxes to toggle drawing normal, skinned, terrain, instanced, trees, etc
	ui_ScrollAreaAddChild(scrollArea, (UIWidget*)ui_LabelCreate("Enable Drawing (will still be updated):", x, y));
	y+=LINE_HEIGHT;
	x+=LINE_INDENT;

	y = addCmdParseCheck(scrollArea, "Normal Objects", "dbgShowNormal", x, y);
	y = addCmdParseCheck(scrollArea, "Skinned Objects", "dbgShowSkinned", x, y);
	y = addCmdParseCheck(scrollArea, "Terrain", "dbgShowTerrain", x, y);
	y = addCmdParseCheck(scrollArea, "Particles", "dbgShowParticle", x, y);
	//y = addCmdParseCheck(scrollArea, "Sprites", "dbgShowSprite", x, y);  Don't let people click this, it will doom them!
	y = addCmdParseCheck(scrollArea, "Primitives (includes Thumbnails)", "dbgShowPrimitive", x, y);
	y = addCmdParseCheck(scrollArea, "Only single triangles", "drawSingleTriangles", x, y);

	x-=LINE_INDENT;


	y+=LINE_HEIGHT;
	y = addPerfModelLODSliders(scrollArea, "", x, y);


	y+=LINE_HEIGHT;
	y = addCmdParseCheck(scrollArea, "Z-Occlusion Visualization", "zocclusionDebug", x, y);

	ui_ScrollAreaSetSize(scrollArea, 1000, y);
	return pTab;
}

static UITab* gfxDebugAddProgrammerTab(void)
{
	UITab *pTab = ui_TabCreate("Programmer");
	UIScrollArea *scrollArea;
	F32 x=0, y=2;

	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	// Various debuggers
	y = addCmdParseCheck(scrollArea, "FPS Histogram", "fpshisto", x, y);
	y = addCmdParseCheck(scrollArea, "Thread Performance", "threadPerf", x, y);

	y = addCmdParseCheck(scrollArea, "Enable ShadowBuffers", "shadowBuffer", x, y);
	y = addCmdParseCheck(scrollArea, "Enable Uberlighting", "uberLighting", x, y);
	y = addCmdParseCheck(scrollArea, "Enable CCLighting", "cclighting", x, y);

	y = addCmdParseCheck(scrollArea, "Disable Instancing", "disableSWInstancing", x, y);
	//y = addCmdParseCheck(scrollArea, "Disable HW Instancing", "disableHWInstancing", x, y);

	return pTab;
}

static UITab* gfxDebugAddAudioTab(void)
{
	UITab *pTab = ui_TabCreate("Audio");
	UIScrollArea *scrollArea;
	F32 x=0, y=2;

	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	// Various debuggers
	y = addCmdParseCheck(scrollArea, "Show Files", "audioShowSkeletonFiles", x, y);
	y = addCmdParseCheck(scrollArea, "Show dAnimBits (audio version)", "audioShowAnimBits", x, y);
	y = addCmdParseCheck(scrollArea, "Show FX (audio version)", "dfxDrawLabelsForAllFX", x, y);
	y+=LINE_HEIGHT/4;

	y+=LINE_HEIGHT;
	ui_ScrollAreaSetSize(scrollArea, 1000, y);

	return pTab;
}

static UITab* gfxDebugAddDynamicsTab(void)
{
	UITab *pTab = ui_TabCreate("Animation");
	UIScrollArea *scrollArea;
	F32 x=0, y=2;

	ui_TabGroupAddTab(gfx_debug_data.tabs, pTab);

	scrollArea = ui_ScrollAreaCreate(0, 0, 1.0, 1.0, 1000, 0, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(scrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(pTab, scrollArea);
	y+=LINE_HEIGHT/2;

	y = addCmdParseCheck(scrollArea, "No Anim", "danimOff", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Transitions", "dynMovementNoTransitions", x, y);
	y = addCmdParseCheck(scrollArea, "Anim Count", "danimShowCount", x, y);
	y = addCmdParseCheck(scrollArea, "Anim Logging", "danimLogging", x, y);
	y = addCmdParseCheck(scrollArea, "Force Base Pose", "danimBasePose", x, y);
	y = addCmdParseCheck(scrollArea, "No Movement Sync", "danimNoMovementSync", x, y);
	y = addCmdParseCheck(scrollArea, "No Character LOD", "danimLODOff", x, y);
	y = addCmdParseCheck(scrollArea, "Allow Player LOD", "danimPlayerLOD", x, y);
	y+=LINE_HEIGHT/4;

	y = addCmdParseCheck(scrollArea, "Anim Bits Show",						"danimShowBits",						x, y);
	y = addCmdParseCheck(scrollArea, "Anim Bits Hide Main Skeleton",		"danimShowBitsHideMainSkeleton",		x, y);
	y = addCmdParseCheck(scrollArea, "Anim Bits Show Sub-Skeletons",		"danimShowBitsShowSubSkeleton",			x, y);
	y = addCmdParseCheck(scrollArea, "Anim Bits Hide Misc Data",			"danimShowBitsHideHead",				x, y);
	y = addCmdParseCheck(scrollArea, "Anim Bits Hide Main Sequencer",		"danimShowBitsHideMainSequencer",		x, y);
	y = addCmdParseCheck(scrollArea, "Anim Bits Show Sub-Sequencer",		"danimShowBitsShowSubSequencer",		x, y);
	y = addCmdParseCheck(scrollArea, "Anim Bits Show Overlay Sequencers",	"danimShowBitsShowOverlaySequencer",	x, y);
	y = addCmdParseCheck(scrollArea, "Anim Bits Show Tracking Ids",			"danimShowBitsShowTrackingIds",			x, y);
	y = addCmdParseCheck(scrollArea, "Anim Bits Hide Movement",				"danimShowBitsHideMovement",			x, y);
	y+=LINE_HEIGHT/4;

	y = addCmdParseCheck(scrollArea, "Draw Skeleton", "danimDrawSkeleton", x, y);
	y = addCmdParseCheck(scrollArea, "Lock Debug Skel", "danimLockDebugSkeleton", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Skeleton Axes", "danimDrawSkeletonAxes", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Skeleton NonCritical", "danimDrawSkeletonNonCritical", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Collision Extents", "danimDrawCollisionExtents", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Visibility Extents", "danimDrawVisibilityExtents", x, y);
	y = addCmdParseCheck(scrollArea, "Show Bone Under Mouse", "danimShowBoneUnderMouse", x, y);
	y+=LINE_HEIGHT/4;
	
	y = addCmdParseCheck(scrollArea, "Draw Ragdoll Setup (Gfx)", "danimDrawRagdollDataGfx", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Ragdoll Setup (Anim)", "danimDrawRagdollDataAnim", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Ragdoll Data (PhysX)", "dfxDrawPhysics", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Client Side Ragdoll", "danimDisableClientSideRagdoll", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Client Ragdoll Initial Velocities", "danimDisableClientSideRagdollInitialVelocities", x, y);
	y+=LINE_HEIGHT/4;

	y = addCmdParseCheck(scrollArea, "Draw Torso Pointing", "danimDrawTorsoPointing", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Torso Pointing", "danimDisableTorsoPointing", x, y);
	y = addCmdParseCheck(scrollArea, "Enable Old Anim. Torso Pointing Fix", "danimEnableOldAnimTorsoPointingFix", x, y);
	y = addCmdParseCheck(scrollArea, "Disable New Anim. Torso Pointing Fix", "danimDisableTorsoPointingFix", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Auto-banking", "danimDisableAutoBanking", x, y);
	y+=LINE_HEIGHT/4;

	y = addCmdParseCheck(scrollArea, "Show Ground Reg Skeleton", "dynAnimShowGroundRegBaseSkeleton", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Ground Reg Height Bump", "dynAnimDisableGroundRegHeightBump", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Ground Reg IK", "dynAnimDiableGroundRegIK", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Ground Reg HyperEx Prevention", "dynAnimDisableGroundRegHyperExPrevention", x, y);
	y+=LINE_HEIGHT/4;

	y = addCmdParseCheck(scrollArea, "Draw Strands", "danimDrawStrands", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Skeleton Strands", "danimDisableSkeletonStrands", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Strand Ground Reg.", "danimDisableStrandGroundReg", x, y);
	y = addCmdParseCheck(scrollArea, "Dyn Only on Strands", "danimDynOnlyStrands", x, y);
	y+=LINE_HEIGHT/4;

	y = addCmdParseCheck(scrollArea, "Draw Terrain Tilt", "danimDrawTerrainTilt", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Terrain Tilt", "danimDisableTerrainTilt", x, y);
	y = addCmdParseCheck(scrollArea, "Disable Terrain Tilt Offset", "danimDisableTerrainTiltOffset", x, y);
	y+=LINE_HEIGHT/4;

	y = addCmdParseCheck(scrollArea, "Draw IK Debug", "danimDebugIK", x, y);
	y = addCmdParseCheck(scrollArea, "Draw IK Targets", "danimDrawIKTargets", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Match Base Skel Data", "danimShowMatchJointsBaseSkeleton", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Bouncers", "danimDrawBouncers", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Impact Triggers", "dfxDrawImpactTriggers", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Bodysock Atlas", "danimDrawBodysockAtlas", x, y);
	y+=LINE_HEIGHT/4;

	y = addCmdParseCheck(scrollArea, "Draw Cloth Normals", "dclothDrawNormals", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Cloth Tangents", "dclothDrawTangentSpace", x, y);
	y = addCmdParseCheck(scrollArea, "Draw Cloth Collision", "dclothDrawCollision", x, y);
	y+=LINE_HEIGHT/4;

	y+=LINE_HEIGHT;
	ui_ScrollAreaSetSize(scrollArea, 1000, y);

	return pTab;
}

static void gfxDebugWindowTick(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS)
{
	char buf[64];
	sprintf(buf, "Bottleneck: %s", gfxGetBottleneckString(gfxGetBottleneck()));
	assert(gfx_debug_data.performance_summary_label);
	ui_WidgetSetTextString(UI_WIDGET(gfx_debug_data.performance_summary_label), buf);
	ui_WindowTick(window, UI_PARENT_VALUES);
	GamePrefStoreInt("GfxDebug_X", (int)window->widget.x);
	GamePrefStoreInt("GfxDebug_Y", (int)window->widget.y);
	GamePrefStoreInt("GfxDebug_ActiveTab", ui_TabGroupGetActiveIndex(gfx_debug_data.tabs));
}

static void gfxDebugUIRefreshTab(int idx)
{
	int i;
	UIActivationFunc orig_func;
	ui_TabGroupRemoveAllTabs(gfx_debug_data.tabs);
	for ( i=0; i < eaSize(&gfx_debug_data.tab_infos); i++ )
	{
		GraphicsDebugTab *tab_info = gfx_debug_data.tab_infos[i];
		if(!tab_info->tab || i == idx)
		{
			if(tab_info->tab)
				ui_TabFree(tab_info->tab);
			tab_info->tab = tab_info->create_func();
		}
		ui_TabGroupAddTab(gfx_debug_data.tabs, tab_info->tab);
	}
	orig_func = gfx_debug_data.tabs->cbChanged;
	gfx_debug_data.tabs->cbChanged = NULL;
	ui_TabGroupSetActiveIndex(gfx_debug_data.tabs, GamePrefGetInt("GfxDebug_ActiveTab", 0));
	gfx_debug_data.tabs->cbChanged = orig_func;
}

static void gfxDebugUITabChangedCB(UITabGroup *pTabs, UserData unused)
{
	int tab_idx = ui_TabGroupGetActiveIndex(gfx_debug_data.tabs);
	GamePrefStoreInt("GfxDebug_ActiveTab", tab_idx);
	gfxDebugUIRefreshTab(tab_idx);
}

static GraphicsDebugTab* gfxDebugUICreateTabInfo(TabCreateFunction create_func)
{
	GraphicsDebugTab *new_tab = calloc(1, sizeof(GraphicsDebugTab));
	new_tab->create_func = create_func;
	return new_tab;
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxDebugUI(void)
{
	if (!gfx_debug_data.window) {
		gfxDebugDisableAccessLevelWarnings(true);
		gfx_debug_data.window = ui_WindowCreate("Graphics Options",
			GamePrefGetInt("GfxDebug_X", 8),
			GamePrefGetInt("GfxDebug_Y", 100),
			320,
			510);
		gfx_debug_data.window->widget.tickF = gfxDebugWindowTick;

		gfx_debug_data.tabs = ui_TabGroupCreate(0, 0, 1.f, 1.f);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gfx_debug_data.tabs), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
		gfx_debug_data.tabs->bEqualWidths = true;
		ui_WindowAddChild(gfx_debug_data.window, gfx_debug_data.tabs);
		ui_TabGroupSetChangedCallback(gfx_debug_data.tabs, gfxDebugUITabChangedCB, NULL);

		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddGeneralTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddObjectsTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddMaterialsTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddLightingTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddDynamicsTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddFXTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddSkyTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddPostProcessingTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddPerformanceTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddProgrammerTab));
		eaPush(&gfx_debug_data.tab_infos, gfxDebugUICreateTabInfo(gfxDebugAddAudioTab));
		gfxDebugUIRefreshTab(-1);

		ui_WindowShow(gfx_debug_data.window);
	} else {
		if (ui_WindowIsVisible(gfx_debug_data.window)) {
			ui_WindowHide(gfx_debug_data.window);
		} else {
			ui_WindowShow(gfx_debug_data.window);
		}
	}
}

/////////////////////////
//Sky Graphics Debug UI
/////////////////////////

typedef struct SkyDebugData
{
	UIWindow *window;
	UITextArea *text_area;
} SkyDebugData;

SkyDebugData gfx_sky_debug_data;

static bool gfxSkyDebugWindowClose(SA_PARAM_NN_VALID UIWindow *window, UserData unused)
{
	gfxSkySetFillingDebugTextFlag(false);
	return true;
}

static void gfxSkyDebugWindowTick(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS)
{
	char *debug_text = gfxSkyGetDebugText();
	assert(gfx_sky_debug_data.text_area);
	if(debug_text)
	{
		UI_EDITABLE(gfx_sky_debug_data.text_area)->keepCursor = true;
		ui_TextAreaSetText(gfx_sky_debug_data.text_area, debug_text);
	}
	ui_WindowTick(window, UI_PARENT_VALUES);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void skyDebug(int val)
{
	if(!!val)
	{
		if(!gfx_sky_debug_data.window)
		{
			//Make the window
			gfx_sky_debug_data.window = ui_WindowCreate("Sky Debug", 100, 100, 500, 800);
			gfx_sky_debug_data.window->widget.tickF = gfxSkyDebugWindowTick;
			gfx_sky_debug_data.text_area = ui_TextAreaCreate("");
			ui_SetActive(UI_WIDGET(gfx_sky_debug_data.text_area), false);
			ui_WindowSetCloseCallback(gfx_sky_debug_data.window,gfxSkyDebugWindowClose,NULL);
			ui_WidgetSetDimensionsEx(UI_WIDGET(gfx_sky_debug_data.text_area), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
			ui_WindowAddChild(gfx_sky_debug_data.window, gfx_sky_debug_data.text_area);
		}
		ui_WindowShow(gfx_sky_debug_data.window);
		gfxSkySetFillingDebugTextFlag(true);
	}
	else
	{
		ui_WindowHide(gfx_sky_debug_data.window);
		gfxSkySetFillingDebugTextFlag(false);
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(skyDebug);
void skyDebugNoParams(void)
{
	skyDebug(true);
}
