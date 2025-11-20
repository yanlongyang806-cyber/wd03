#ifndef NO_EDITORS

#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "EditorManager.h"
#include "WorldGrid.h"
#include "WorldEditorOperations.h"
#include "MultiEditFieldContext.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static bool wleAEChildCurvePropsExclude(GroupTracker *pTracker)
{
	GroupTracker *pParent;
	bool bHasParentCurve = false;

	if(wleNeedsEncounterPanels(pTracker->def) || pTracker->def->property_structs.physical_properties.bOnlyAVolume)
		return true;

	pParent = pTracker;
	while (pParent) {
		pParent = pParent->parent;
		if (pParent && pParent->def && pParent->def->property_structs.curve) {
			bHasParentCurve = true;
			break;
		}
	}
	
	if (!bHasParentCurve)
		return true;

	return false;
}

int wleAECurveReload(EMPanel *panel, EditorObject *edObj)
{
	static WleAEPropStructData pPropData = {"ChildCurve", parse_WorldChildCurve};
	WorldChildCurve **ppProps = NULL;
	WorldChildCurve *pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldChildCurve**)wleAEGetSelectedDataFromPath("ChildCurve", wleAEChildCurvePropsExclude, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&ppProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing))
		pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_CurveChildProps", ppProps, ppProps, parse_WorldChildCurve);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	if(pProp) {

		MEContextAddEnum(kMEFieldType_Combo, CurveChildTypeEnum,	"ChildType",		"Child Type",				"See Tom Yedwab for information.");

		MEContextAddMinMax(kMEFieldType_SliderText, 0, 30000, 0.1,	"GeometryLength",	"Geometry Length",			"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_SliderText, 1, 1000, 0.1,	"RepeatLength",		"Default Spacing",			"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 1, 0.05,		"CurveFactor",		"Curve Factor",				"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 1, 0.05,		"StretchFactor",	"Stretch Factor",			"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 100, 0.05,	"XScale",			"Stretch X",				"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 360, 1,		"UVRotation",		"UV Rotate",				"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 30000, 0.1,	"UVScale",			"UV Scale",					"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_Spinner, 1, 5000, 1,		"MaxPointCount",	"Max Points",				"See Tom Yedwab for information.");
				

		MEContextAddSpacer();
		MEContextAddLabel(											"Offset",			"Offset:",					"Offsets");
		MEContextIndentRight();
		MEContextAddMinMax(kMEFieldType_SliderText, -100, 100, 0.05,"BeginOffset",		"Begin",					"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_SliderText, -100, 100, 0.05,"EndOffset",		"End",						"See Tom Yedwab for information.");
		MEContextIndentLeft();

		MEContextAddSpacer();
		MEContextAddLabel(											"Padding",			"Padding:",					"Padding");
		MEContextIndentRight();
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 100, 0.05,	"BeginPad",			"Begin",					"See Tom Yedwab for information.");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 100, 0.05,	"EndPad",			"End",						"See Tom Yedwab for information.");
		MEContextIndentLeft();

		MEContextAddSpacer();
		MEContextAddSimple(kMEFieldType_Check,						"DeformGeometry",	"Deform Geometry",			"See Tom Yedwab for information.");
		MEContextAddSimple(kMEFieldType_Check,						"NormalizeGeometry","Normalize Control Points",	"See Tom Yedwab for information.");
		MEContextAddSimple(kMEFieldType_Check,						"Attachable",		"Attachable",				"See Tom Yedwab for information.");
		MEContextAddSimple(kMEFieldType_Check,						"Linearize",		"Linearize",				"See Tom Yedwab for information.");
		MEContextAddSimple(kMEFieldType_Check,						"AvoidGaps",		"Inherit Gaps",				"See Tom Yedwab for information.");
		MEContextAddSimple(kMEFieldType_Check,						"Reverse",			"Reverse Curve",			"See Tom Yedwab for information.");
		MEContextAddSimple(kMEFieldType_Check,						"ResetUp",			"Reset Up",					"See Tom Yedwab for information.");
		MEContextAddSimple(kMEFieldType_Check,						"ExtraPoint",		"Extra Endpoint",			"See Tom Yedwab for information.");
		MEContextAddSimple(kMEFieldType_Check,						"NoBoundsOffset",	"No Bounds Offset",			"See Tom Yedwab for information.");


		pContext->iXDataStart = MEFC_DEFAULT_X_DATA_START/2;
		MEContextAddButton("Remove Curve Properties", NULL, wleAERemovePropsToSelection, &pPropData, 	"AddRemove",		NULL,				"Remove Curve Properties from this object.");
	} else {
		pContext->iXDataStart = 0;
		MEContextAddButton("Add Curve Properties", NULL, wleAEAddPropsToSelection, &pPropData, 			"AddRemove",		NULL,				"Add Curve Properties to this object.");
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_CurveChildProps");

	return (pProp ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED);	
}

#endif