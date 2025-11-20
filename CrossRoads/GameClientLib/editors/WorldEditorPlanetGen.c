#ifndef NO_EDITORS

#include "WorldEditorAttributesPrivate.h"

#include "WorldGrid.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "MultiEditFieldContext.h"

#include "WorldEditorAttributesHelpers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static bool wleAEPlanetGenExclude(GroupTracker *pTracker)
{
	GroupDef *pDef = pTracker->def;
	if(!pDef->property_structs.planet_properties)
		return true;
	return false;
}

static void wleAEPlanetPropChangedCB(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	WorldPlanetProperties *pOld = pOldProps->planet_properties;
	WorldPlanetProperties *pNew = pNewProps->planet_properties;
	if(pOld->has_atmosphere != pNew->has_atmosphere) {
		if (pNew->has_atmosphere) {
			// default atmosphere values
			pNew->atmosphere.planet_radius = 10.0f;
			pNew->atmosphere.atmosphere_thickness = 0.25f;
		} else {
			pNew->atmosphere.planet_radius = 0.1f;
			pNew->atmosphere.atmosphere_thickness = 0;
		}
	}
}

int wleAEPlanetGenReload(EMPanel *panel, EditorObject *edObj)
{
	static WleAEPropStructData pPropData = {"Planet", parse_WorldPlanetProperties};
	WorldPlanetProperties **ppProps = NULL;
	WorldPlanetProperties *pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldPlanetProperties**)wleAEGetSelectedDataFromPath("Planet", wleAEPlanetGenExclude, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&ppProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing)) {
		pProp = ppProps[0]; 
	} else {
		return WLE_UI_PANEL_INVALID;
	}

	pContext = MEContextPushEA("WorldEditor_PlanetProps", ppProps, ppProps, parse_WorldPlanetProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, wleAEPlanetPropChangedCB);

	MEContextAddMinMax(kMEFieldType_SliderText, 0, 100000, 1,		"GeometryRadius",		"Geometry Radius",	"The physical radius of the planet geometry.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0, 100000, 1,		"CollisionRadius",		"Collision Radius",	"The physical radius of the planet collision geometry.");

	MEContextAddSpacer();
	MEContextAddSimple(kMEFieldType_Check,							"HasAtmosphere",		"Has Atmosphere",	"Whether this planet has an atmosphere.");
	if(!MEContextFieldDiff("HasAtmosphere") && pProp->has_atmosphere) {
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 100000, 1,	"PlanetRadius",			"Planet Radius",	"The radius of the planet used for atmospherics calculations.");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 100000, 0.05,"AtmosphereThickness",	"Atm Thickness",	"The thickness of the atmosphere used for atmospherics calculations.");
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_PlanetProps");

	return WLE_UI_PANEL_OWNED;
}

#endif