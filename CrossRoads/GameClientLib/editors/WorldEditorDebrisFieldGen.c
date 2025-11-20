#ifndef NO_EDITORS

#include "WorldEditorAttributesPrivate.h"

#include "WorldGrid.h"
#include "EditorManager.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "ObjectLibrary.h"
#include "EditLibUIUtil.h"
#include "MultiEditFieldContext.h"

#include "WorldEditorAttributesHelpers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

int wleAEDebrisFieldGenReload(EMPanel *panel, EditorObject *edObj)
{
	static WleAEPropStructData pPropData = {"DebrisField", parse_WorldDebrisFieldProperties};
	WorldDebrisFieldProperties **ppProps = NULL;
	WorldDebrisFieldProperties *pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldDebrisFieldProperties**)wleAEGetSelectedDataFromPath("DebrisField", NULL, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&ppProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing)) {
		pProp = ppProps[0]; 
	} else {
		return WLE_UI_PANEL_INVALID;
	}

	pContext = MEContextPushEA("WorldEditor_DebrisFieldProps", ppProps, ppProps, parse_WorldDebrisFieldProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddPicker(OBJECT_LIBRARY_DICT, "Object Picker",			"GroupRef",			"Debris Model",			"The model used in the Debris Field.");
	MEContextAddMinMax(kMEFieldType_SliderText,	0.0f, 500.0f, 1.0f,		"Density",			"Density",				"Number of objects in 512ft^3 area.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.0f, 1.0f, 0.01f,		"Falloff",			"Falloff",				"0-1 where 0 is start falling off immediately, and 1 is no fall off.");
	MEContextAddSimple(kMEFieldType_Check,								"RandomRotation",	"Random Alignment",		"Randomly rotate the objects when placing.");
	MEContextAddSimple(kMEFieldType_Check,								"EvenDistribution",	"Even Distribution",	"Non random locations.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.0f, 3000.0f, 0.01f,	"RandomOffset",		"Random Offset",		"When using Even Distribution, how much in feet to vary the positions.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0.001f, 100000, 1,		"CenterOccluder",	"Center Occluder Size",	"Ensures that no objects are places inside a center radius.");
	MEContextAddSimple(kMEFieldType_Check,								"Box",				"Fit in Box",			"Containing box of shape.");

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_DebrisFieldProps");

	return WLE_UI_PANEL_OWNED;
}

#endif
