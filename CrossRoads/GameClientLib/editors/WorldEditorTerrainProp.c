#include "WorldEditorTerrainProp.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorOperations.h"
#include "wlTerrainBrush.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "WorldGrid.h"
#include "WorldEditorUtil.h"
#include "MultiEditField.h"
#include "MultiEditFieldContext.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* MAIN
********************/

static WorldTerrainProperties* wleTerrainGetDefaultProps()
{
	static WorldTerrainProperties *pDefaultProps = NULL;
	if(!pDefaultProps)
		pDefaultProps = StructCreate(parse_WorldTerrainProperties);
	return pDefaultProps;
}

static void wleAETerrainPropAddCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pTracker->def) {
		pData->pTracker->def->property_structs.terrain_properties.bShowPanel = 1;
	}
}

void wleAETerrainPropAdd(UserData *pUnused, UserData *pUnused2)
{
	wleAEApplyToSelection(wleAETerrainPropAddCB, NULL, NULL);
}

static void wleAETerrainPropRemoveCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pTracker->def) {
		StructCopy(parse_WorldTerrainProperties, wleTerrainGetDefaultProps(), &pData->pTracker->def->property_structs.terrain_properties, 0, 0, 0);
	}
}

void wleAETerrainPropRemove(UserData *pUnused, UserData *pUnused2)
{
	wleAEApplyToSelection(wleAETerrainPropRemoveCB, NULL, NULL);
}

static bool wleAETerrainPropsExclude(GroupTracker *pTracker)
{
	return wleNeedsEncounterPanels(pTracker->def);
}

int wleAETerrainPropReload(EMPanel *panel, EditorObject *edObj)
{
	WorldTerrainProperties **ppProps = NULL;
	WorldTerrainProperties *pProp;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldTerrainProperties**)wleAEGetSelectedDataFromPath("Terrain", wleAETerrainPropsExclude, &iRetFlags);
	if(eaSize(&ppProps) == 0)
		return WLE_UI_PANEL_INVALID;

	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_Terrain", ppProps, ppProps, parse_WorldTerrainProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddSimple(kMEFieldType_Check,											"TerrainObject",					"Terrain Object",			"Allows use on the Terrain through the Object Brush.");
	MEContextAddSimple(kMEFieldType_Check,											"SnapToTerrainHeight",				"Snap to Terrain Height",	"Snaps placed objects to terrain height on the same layer.");
	if (pProp->bSnapToTerrainHeight)
	{
		MEContextAddSimple(kMEFieldType_Check,										"SnapToNormal",						"Snap to Terrain Normal",	"Snaps placed objects to terrain normal on the same layer when snapping them to terrain height.");
	}
	MEContextAddSimple(kMEFieldType_Check,											"GetTerrainColor",					"Use Terrain Color",		"Tints the object with the terrain color.");
	MEContextAddSpacer();
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 1.f, 0.05f,					"IntensityVariation",				"Terrain Random Intensity", "Amount of intensity variation. (0-1)");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 1.f, 0.1f,						"SnapToTerrainNormal",				"Snap to Terrain Normal",	"Snaps the objects to the terrain normal. (0-1)");
	MEContextAddSpacer();
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 100.f, 0.1f,					"ScaleMin",							"Terrain Scale Min",		"Minimum scale factor for terrain painted objects.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 100.f, 0.1f,					"ScaleMax",							"Terrain Scale Max",		"Maximum scale factor for terrain painted objects.");
	MEContextAddSpacer();
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 10000.f, 1.f,					"ExcludePriority",					"Exclude Priority",			"Priority of this object to exclude others. (Higher priority wins)");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 100.f, 0.1f,					"ExcludePriorityScale",				"Exclude Priority Scale",	"Scale factor (relative to the default) of the placement priority on the object.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 5000.f, 0.1f,					"ExcludeSame",						"Exclude Same Distance",	"Distance to exclude other objects of the same priority.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 5000.f, 0.1f,					"ExcludeOthersBegin",				"Exclude Other Start",		"Distance to exclude other objects of a lesser priority. (Start of falloff)");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 5000.f, 0.1f,					"ExcludeOthersEnd",					"Exclude Other End",		"Distance to end of falloff for excluding other objects.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0, 1000, 1,							"MultiExclusionVolumesRequired",	"Multi-Excluder Count",		"If non-zero, turns this object into a multi-excluder. Each child will be placed individually. This number specifies the number of children which are non-optional at the top of the child list.");
	MEContextAddMinMax(kMEFieldType_SliderText,	0, 1000, 0.01,						"MultiExclusionVolumesDensity",		"Multi-Excluder Density",	"The probability in the range 0-1 of an optional child being selected.");
	MEContextAddEnum  (kMEFieldType_Combo, GenesisMultiExcludeRotTypeEnum,			"MultiExclusionVolumesRotation",	"Multi-Excluder Rotation",	"How children will be rotated relative to the object.");
	MEContextAddSpacer();
	MEContextAddSimple(kMEFieldType_Check,											"VaccuFormMe",						"VaccuForm Me!",			"Flags the model for baking into the terrain.");
	if(pProp->bVaccuFormMe) {
		MEContextAddMinMax(kMEFieldType_SliderText, 0.f, 5000.f, 0.1f,				"VaccuFormFalloff",					"VaccuForm Radius",			"Falloff radius for baking into the terrain.");
		MEContextAddDict(kMEFieldType_ValidatedTextEntry, MULTI_BRUSH_DICTIONARY,	"VaccuFormBrush",					"VaccuForm Brush",			"Multibrush name to apply after baking into terrain.");
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_Terrain");

	if(StructCompare(parse_WorldTerrainProperties, wleTerrainGetDefaultProps(), pProp, 0, 0, 0) == 0)
		return WLE_UI_PANEL_UNOWNED;
	return WLE_UI_PANEL_OWNED;
}
