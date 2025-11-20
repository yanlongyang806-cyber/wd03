#ifndef NO_EDITORS

#include "WorldEditorSoundAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "StringCache.h"

#include "WorldGrid.h"
#include "SoundLib.h"
#include "WorldEditorOperations.h"
#include "WorldEditorOptions.h"
#include "WorldEditorUtil.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "MultiEditFieldContext.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static bool wleAESoundEventCritCheck(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, void *data)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		bool ret;
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		const char *eventName = NULL;
		
		if (tracker && tracker->def && tracker->def->property_structs.sound_sphere_properties)
			eventName = tracker->def->property_structs.sound_sphere_properties->pcEventName;
		if (!eventName && tracker && tracker->def && tracker->def->property_structs.client_volume.sound_volume_properties)
			eventName = tracker->def->property_structs.client_volume.sound_volume_properties->event_name;

		if (wleCriterionStringTest(eventName ? eventName : "", val, cond, &ret))
			return ret;
	}
	return false;
}

int wleAESoundReloadRoom(EMPanel *panel)
{
	static WleAEPropStructData pPropData = {"SoundVolume", parse_WorldSoundVolumeProperties};
	WorldSoundVolumeProperties **ppProps = NULL;
	WorldSoundVolumeProperties *pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldSoundVolumeProperties**)wleAEGetSelectedDataFromPath("SoundVolume", NULL, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;
	if(eaSize(&ppProps) > 0 && (iRetFlags & WleAESelectedDataFlags_SomeMissing))
		return WLE_UI_PANEL_INVALID;
	if(eaSize(&ppProps) <= 0)
		return WLE_UI_PANEL_INVALID;
	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_RoomSound", ppProps, ppProps, parse_WorldSoundVolumeProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddList(kMEFieldType_ValidatedTextEntry, sndGetEventListStatic(),		"EventName",				"Sound Event",			"The first sound event associated with this object.");
	MEContextIndentRight();
	MEContextAddSimple(kMEFieldType_ValidatedTextEntry,								"EventNameOverrideParam",	"Event Override Param",	"The override parameter name to use for the sound event. The value of this parameter will be used for the Sound Event, if found.");
	MEContextIndentLeft();
	MEContextAddList(kMEFieldType_ValidatedTextEntry, sndGetEventListStatic(),		"MusicName",				"Music Event",			"The second sound event associated with this object.");
	MEContextAddList(kMEFieldType_ValidatedTextEntry, sndGetDSPListStatic(),		"DSPName",					"DSP",					"The sound dsp associated with this object.");
	MEContextIndentRight();
	MEContextAddSimple(kMEFieldType_ValidatedTextEntry,								"DSPNameOverrideParam",		"DSP Override Param",	"The override parameter name to use for the sound dsp. The value of this parameter will be used for the DSP, if found.");
	MEContextIndentLeft();
	MEContextAddMinMax(kMEFieldType_Spinner, 0, 100, 1,								"Multiplier",				"Multiplier",			"The distance multipler of this volume.");
	MEContextAddMinMax(kMEFieldType_Spinner, 0, 100, 1,								"Priority",					"Priority",				"The priority of this sound volume.");

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_RoomSound");

	return WLE_UI_PANEL_OWNED;
}

int wleAESoundReloadPortal(EMPanel *panel)
{
	static WleAEPropStructData pPropData = {"SoundConn", parse_WorldSoundConnProperties};
	WorldSoundConnProperties **ppProps = NULL;
	WorldSoundConnProperties *pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldSoundConnProperties**)wleAEGetSelectedDataFromPath("SoundConn", NULL, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;
	if(eaSize(&ppProps) > 0 && (iRetFlags & WleAESelectedDataFlags_SomeMissing))
		return WLE_UI_PANEL_INVALID;
	if(eaSize(&ppProps) <= 0)
		return WLE_UI_PANEL_INVALID;
	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_PortalSound", ppProps, ppProps, parse_WorldSoundConnProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddList(kMEFieldType_ValidatedTextEntry, sndGetDSPListStatic(), 		"DSPName",		"DSP",				"The sound dsp associated with this object.");
	MEContextAddMinMax(kMEFieldType_SliderText, 0, pProp->max_range, 0.1,			"MinRange",		"Min Range",		"Minimum Range of incoming sound");
	MEContextAddMinMax(kMEFieldType_SliderText, pProp->min_range, 1000, 0.1,		"MaxRange",		"Max Range",		"Maximum Range of incoming sound");

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_PortalSound");

	return WLE_UI_PANEL_OWNED;
}

static void wleAESoundReloadSphereChanged(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	if(pNewProps->sound_sphere_properties && !pNewProps->sound_sphere_properties->bExclude) {
		pNewProps->sound_sphere_properties->pcDSPName = NULL;
		pNewProps->sound_sphere_properties->fMultiplier = 1;
		pNewProps->sound_sphere_properties->fPriority = 0;
	}
}

bool wleAESoundReloadSphere(EMPanel *panel)
{
	static WleAEPropStructData pPropData = {"SoundSphere", parse_WorldSoundSphereProperties};
	WorldSoundSphereProperties **ppProps = NULL;
	WorldSoundSphereProperties *pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldSoundSphereProperties**)wleAEGetSelectedDataFromPath("SoundSphere", NULL, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&ppProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing))
		pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_SoundSphere", ppProps, ppProps, parse_WorldSoundSphereProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, wleAESoundReloadSphereChanged);

	if(pProp) {

		MEContextAddList(kMEFieldType_ValidatedTextEntry, sndGetEventListStatic(),	 	"EventName",		"Event",		"The sound event associated with this object.");
		MEContextAddSimple(kMEFieldType_TextEntry,										"SoundGroup",		"Group",		"The sound group associated with this object.");
		MEContextAddMinMax(kMEFieldType_Spinner, 0, 1000, 1,							"SoundGroupOrd",	"Group Order",	"Sort order of sound group");
		
		MEContextAddSpacer();
		MEContextAddSimple(kMEFieldType_Check,											"Exclude",			"Exclusion",	"Whether or not this event excludes external sounds (not used in 2.0).");

		if(!MEContextFieldDiff("Exclude") && pProp->bExclude) {
			MEContextAddList(kMEFieldType_ValidatedTextEntry, sndGetDSPListStatic(),	"DSPName",			"DSP",			"The sound dsp associated with this object.");
			MEContextAddMinMax(kMEFieldType_Spinner, 0, 100, 1,							"Multiplier",	"Multiplier",		"The distance multipiler of this soundsphere.");
			MEContextAddMinMax(kMEFieldType_Spinner, 0, 100, 1,							"Priority",		"Priority",			"The priority of this soundsphere.");
		}

		pContext->iXDataStart = MEFC_DEFAULT_X_DATA_START/2;
		MEContextAddButton("Remove Sound Sphere", NULL, wleAERemovePropsToSelection, &pPropData, 	"AddRemove",		NULL,				"Remove Sound Sphere Properties from this object.");
	} else {
		pContext->iXDataStart = 0;
		MEContextAddButton("Add Sound Sphere", NULL, wleAEAddPropsToSelection, &pPropData, 			"AddRemove",		NULL,				"Add Sound Sphere Properties to this object.");
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_SoundSphere");

	return (pProp ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED);
}

int wleAESoundReload(EMPanel *panel, EditorObject *edObj)
{
	int ret;
	static WleCriterion *pCrit = NULL;

	// setup filter criteria
	if(!pCrit) {
		pCrit = StructCreate(parse_WleCriterion);
		eaiPush(&pCrit->allConds, WLE_CRIT_EQUAL);
		eaiPush(&pCrit->allConds, WLE_CRIT_NOT_EQUAL);
		eaiPush(&pCrit->allConds, WLE_CRIT_CONTAINS);
		eaiPush(&pCrit->allConds, WLE_CRIT_BEGINS_WITH);
		eaiPush(&pCrit->allConds, WLE_CRIT_ENDS_WITH);
		pCrit->checkCallback = wleAESoundEventCritCheck;
		pCrit->propertyName = StructAllocString("Sound Event");
		wleCriterionRegister(pCrit);
	}

	MEContextPush("WorldEditor_SoundProps", NULL, NULL, NULL);

	ret = wleAESoundReloadRoom(panel);
	if(ret == WLE_UI_PANEL_INVALID) {
		ret = wleAESoundReloadPortal(panel);
		if(ret == WLE_UI_PANEL_INVALID) {
			ret = wleAESoundReloadSphere(panel);
		}
	}

	MEContextPop("WorldEditor_SoundProps");

	return ret;
}

#endif