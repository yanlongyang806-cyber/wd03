#ifndef NO_EDITORS

#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "EditorManager.h"
#include "WorldGrid.h"
#include "WorldEditorOperations.h"
#include "wlModel.h"
#include "EditLibUIUtil.h"
#include "WorldEditorUtil.h"
#include "MultiEditFieldContext.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static bool wleAEWindPropsExclude(GroupTracker *pTracker)
{
	int i;
	bool hasWind = false;
	GroupDef *pDef = pTracker->def;
	if(wleNeedsEncounterPanels(pTracker->def))
		return true;
	if(pDef->property_structs.physical_properties.bForceTrunkWind)
		return false;
	if(!pDef->model)
		return true;
	for (i = 0; i < eaSize((ccEArrayHandle*) &pTracker->def->model->model_lods); i++) {
		ModelLOD *modelLOD = modelLoadLOD(pTracker->def->model, i);
		if (modelLOD && modelLOD->data && !!(modelLOD->data->process_time_flags & (MODEL_PROCESSED_HAS_WIND | MODEL_PROCESSED_HAS_TRUNK_WIND))) {
			hasWind = true;
			break;
		}
	}
	if(!hasWind)
		return true;
	return false;
}

int wleAEWindReload(EMPanel *panel, EditorObject *edObj)
{
	WorldWindProperties **ppProps = NULL;
	WorldWindProperties *pProp;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldWindProperties**)wleAEGetSelectedDataFromPath("Wind", wleAEWindPropsExclude, &iRetFlags);
	if(eaSize(&ppProps) == 0 || (iRetFlags & WleAESelectedDataFlags_SomeMissing))
		return WLE_UI_PANEL_INVALID;

	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_WindProperties", ppProps, ppProps, parse_WorldWindProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddMinMax(kMEFieldType_SliderText, 0, 2, 0.1,		"EffectAmount",		"Wind Effect Amount",	"See Tom Yedwab for more information.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0, 2, 0.1,		"Bendiness",		"Bendiness",			"See Tom Yedwab for more information.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0, 2, 0.1,		"Rustling",			"Rustling",				"See Tom Yedwab for more information.");
	MEContextAddMinMax(kMEFieldType_SliderText, -2000, 2000, 1,	"PivotOffset",		"Pivot Offset",			"See Tom Yedwab for more information.");

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_WindProperties");

	return WLE_UI_PANEL_OWNED;
}

int wleAEWindSourceReload(EMPanel *panel, EditorObject *edObj)
{
	WorldWindSourceProperties **ppProps = NULL;
	WorldWindSourceProperties *pProp;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldWindSourceProperties**)wleAEGetSelectedDataFromPath("WindSource", NULL, &iRetFlags);
	if(eaSize(&ppProps) == 0 || (iRetFlags & WleAESelectedDataFlags_SomeMissing))
		return WLE_UI_PANEL_INVALID;

	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_WindSourceProperties", ppProps, ppProps, parse_WorldWindSourceProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddEnum(kMEFieldType_Combo, WorldWindEffectTypeEnum,				"EffectType",			"Effect Type",			"The type of effect this wind source has.");

	MEContextAddMinMax(kMEFieldType_SliderText, 0, 10, 0.1,						"Speed",				"Speed",				"The base wind speed.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0, 10, 0.1,						"SpeedVariation",		"Speed Variation",		"The maxium amount of speed variation above or below the base speed.");
	MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 1, 0.05,					"DirectionVariation",	"Direction Variation",	"The maximum amount of variation from the base direction in each dimension.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0, 4, 0.01,						"Turbulence",			"Turbulence",			"The speed at which the wind force changes over time.");

	MEContextAddSpacer();
	MEContextAddLabel(															"RadiusLabel",			"Radius",				"Limits of the wind effect.");
	MEContextIndentRight();
	MEContextAddMinMax(kMEFieldType_SliderText, 0, pProp->radius, 0.5,			"RadiusInner",			"Inner",				"The inner limit of the wind effect. Within the radius the wind has a constant effect.");
	MEContextAddMinMax(kMEFieldType_SliderText, pProp->radius_inner, 2000, 0.5,	"Radius",				"Outer",				"The outer limit of the wind effect. The wind source fades to zero between the inner and outer radius");
	MEContextIndentLeft();


	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_WindSourceProperties");

	return WLE_UI_PANEL_OWNED;
}

#endif
