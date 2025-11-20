#ifndef NO_EDITORS

#include "WorldEditorPatrolAttributes.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorClientMain.h"
#include "WorldGrid.h"
#include "EditorManager.h"
#include "MultiEditFieldContext.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void wleAEPatrolRouteAddPoint(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	TrackerHandle *pTrackerHandle = trackerHandleCreate(pTracker);
	wleTrackerDeselectAll();
	wlePatrolPointPlace(pTrackerHandle);
	trackerHandleDestroy(pTrackerHandle);
}

static void wleAEPatrolRouteAddPointClicked(UIButton *button, UserData unused)
{
	wleAECallFieldChangedCallback(NULL, wleAEPatrolRouteAddPoint);
}

static void wleAEPatrolRouteReversePath(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	wleTrackerDeselectAll();
	eaReverse(&pNewProps->patrol_properties->patrol_points);
}

static void wleAEPatrolRouteReversePathClicked(UIButton *button, UserData unused)
{
	wleAECallFieldChangedCallback(NULL, wleAEPatrolRouteReversePath);
}

int wleAEPatrolReload(EMPanel *panel, EditorObject *edObj)
{
	WorldPatrolProperties **ppProps = NULL;
	WorldPatrolProperties *pProp;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldPatrolProperties**)wleAEGetSelectedDataFromPath("PatrolRoute", NULL, &iRetFlags);
	if(eaSize(&ppProps) == 0 || (iRetFlags & WleAESelectedDataFlags_SomeMissing))
		return WLE_UI_PANEL_INVALID;

	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_PatrolRouteProperties", ppProps, ppProps, parse_WorldPatrolProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddEnum(kMEFieldType_Combo, WorldPatrolRouteTypeEnum,							"RouteType",	"Route Type",	"How the patrol is traversed.");
	if(eaSize(&ppProps) == 1) {
		MEContextAddSpacer();
		pContext->iXDataStart = pContext->iXPos;
		MEContextAddButton("Add point", NULL, wleAEPatrolRouteAddPointClicked, NULL,		"AddPoint",		NULL,			"Adds a point to the patrol route.");
		MEContextAddButton("Reverse Path", NULL, wleAEPatrolRouteReversePathClicked, NULL,	"ReversePath",	NULL,			"First point becomes last and last becomes first.");
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_PatrolRouteProperties");

	return WLE_UI_PANEL_OWNED;
}

#endif