#ifndef NO_EDITORS

#include "WorldEditorGizmos.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorUI.h"
#include "WorldEditorOperations.h"
#include "WorldEditorPrivate.h"
#include "WorldEditorOptions.h"
#include "WorldEditorAttributes.h"
#include "WorldEditorLightAttributes.h"
#include "EditorManager.h"
#include "EditorObject.h"
#include "EditLibGizmos.h"
#include "EditLibGizmosToolbar.h"
#include "EditorPrefs.h"
#include "GfxPrimitive.h"
#include "inputLib.h"
#include "WorldLib.h"
#include "WorldGrid.h"

// TODO: rewrite the light-related gizmos
/********************
* LIGHT EDITING
********************/

/*
 * This function takes care of processing mouse input to alter the various properties of lights.
 */
static void handleLightInput(TrackerHandle *handle)
{
	EditorObject *editing = wleAEGetSelected();
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	float coneMin = 0, coneMax = 0, cone2Min = 0, cone2Max = 0;
	float radiusMin = 0, radiusMax = 0, shadowNearDist = 0;
	Color paleWhite = colorFromRGBA(0xFFFFFF33);
	Color red = colorFromRGBA(0xFF0000EE);
	Color green = colorFromRGBA(0x00FF00EE);
	static bool inOp = false;
	Mat4 drawMat;
	bool updateLightPanel = (editing && editing->type->objType == EDTYPE_TRACKER && trackerFromTrackerHandle(editing->obj) == tracker);

	if (!tracker || !tracker->def)
		return;
	trackerGetMat(tracker, drawMat);

	// TODO: get light properties on activate function of the gizmos when gizmos are rewritten
	// find the appropriate light properties
	if (editState.trackerType == EditSpotLight)
	{
		groupGetLightPropertyFloat(tracker, NULL, NULL, "LightConeInner", &coneMin);
		groupGetLightPropertyFloat(tracker, NULL, NULL, "LightConeOuter", &coneMax);
	}
	else if (editState.trackerType == EditProjectorLight)
	{
		groupGetLightPropertyFloat(tracker, NULL, NULL, "LightConeInner", &coneMin);
		groupGetLightPropertyFloat(tracker, NULL, NULL, "LightConeOuter", &coneMax);
		groupGetLightPropertyFloat(tracker, NULL, NULL, "LightCone2Inner", &cone2Min);
		groupGetLightPropertyFloat(tracker, NULL, NULL, "LightCone2Outer", &cone2Max);
		groupGetLightPropertyFloat(tracker, NULL, NULL, "LightShadowNearDist", &shadowNearDist);
	}
	groupGetLightPropertyFloat(tracker, NULL, NULL, "LightRadiusInner", &radiusMin);
	groupGetLightPropertyFloat(tracker, NULL, NULL, "LightRadius", &radiusMax);

	// TODO: internalize a lot of gizmo values, implement internal click detection
	// handle transformation
	// scaling inner radius
	if (editState.transformMode == EditScaleMin)
	{
		// draw gizmo(s)
		RadialGizmoDraw(editState.radGizmo, drawMat[3], radiusMin, red, editState.trackerType == EditPointLight);
		if (editState.trackerType == EditPointLight)
			gfxDrawSphere3D(drawMat[3], radiusMax, -1, paleWhite, 1);
		else if (!RadialGizmoGetActive(editState.radGizmo))
		{
			// if gizmo isn't active, draw light wireframes
			if (editState.trackerType == EditSpotLight)
			{
				gfxDrawCone3D(drawMat, radiusMin, RAD(coneMin), -1, red);
				gfxDrawCone3D(drawMat, radiusMax, RAD(coneMax), -1, paleWhite);
			}
			else if (editState.trackerType == EditProjectorLight)
			{
				gfxDrawPyramid3D(drawMat, shadowNearDist, radiusMin, RAD(coneMin), RAD(cone2Min), ColorTransparent, red);
				gfxDrawPyramid3D(drawMat, shadowNearDist, radiusMax, RAD(coneMax), RAD(cone2Max), ColorTransparent, paleWhite);
			}
		}
		if (mouseDown(MS_LEFT))
			RadialGizmoHandleInput(editState.radGizmo, drawMat[3], radiusMin);

		RadialGizmoActivate(editState.radGizmo, inOp);
		RadialGizmoDrag(editState.radGizmo, drawMat[3], &radiusMin);
		if (radiusMin > radiusMax)
			radiusMin = radiusMax;

	}

	// scaling outer radius
	if (editState.transformMode == EditScale)
	{
		RadialGizmoDraw(editState.radGizmo, drawMat[3], radiusMax, green, editState.trackerType == EditPointLight);
		if (editState.trackerType == EditPointLight)
			gfxDrawSphere3D(drawMat[3], radiusMin, -1, paleWhite, 1);
		else if (!RadialGizmoGetActive(editState.radGizmo))
		{
			if (editState.trackerType == EditSpotLight)
			{
				gfxDrawCone3D(drawMat, radiusMin, RAD(coneMin), -1, paleWhite);
				gfxDrawCone3D(drawMat, radiusMax, RAD(coneMax), -1, green);
			}
			else if (editState.trackerType == EditProjectorLight)
			{
				gfxDrawPyramid3D(drawMat, shadowNearDist, radiusMin, RAD(coneMin), RAD(cone2Min), ColorTransparent, paleWhite);
				gfxDrawPyramid3D(drawMat, shadowNearDist, radiusMax, RAD(coneMax), RAD(cone2Max), ColorTransparent, green);
			}
		}
		if (mouseDown(MS_LEFT))
			RadialGizmoHandleInput(editState.radGizmo, drawMat[3], radiusMax);

		RadialGizmoActivate(editState.radGizmo, inOp);
		RadialGizmoDrag(editState.radGizmo, drawMat[3], &radiusMax);
		if (radiusMax < radiusMin)
			radiusMax = radiusMin;

	}

	// modify min cone
	else if (editState.transformMode == EditMinCone)
	{
		if (editState.trackerType == EditSpotLight)
		{
			float changed = coneMin;
			ConeGizmoDraw(editState.coneGizmo, drawMat, radiusMin, RAD(coneMin), red);
			gfxDrawCone3D(drawMat, radiusMax, RAD(coneMax), -1, paleWhite);
			if (mouseDown(MS_LEFT))
				ConeGizmoHandleInput(editState.coneGizmo, coneMin);
			ConeGizmoActivate(editState.coneGizmo, inOp);
			ConeGizmoDrag(editState.coneGizmo, &coneMin);
			if (coneMin > coneMax)
				coneMin = coneMax;
		}
		else if (editState.trackerType == EditProjectorLight)
		{
			float changed1 = coneMin, changed2 = cone2Min;
			PyramidGizmoDraw(editState.pyramidGizmo, drawMat, radiusMin, RAD(coneMin), RAD(cone2Min), red);
			gfxDrawPyramid3D(drawMat, shadowNearDist, radiusMax, RAD(coneMax), RAD(cone2Max), ColorTransparent, paleWhite);
			if (mouseDown(MS_LEFT))
				PyramidGizmoHandleInput(editState.pyramidGizmo, coneMin, cone2Min);
			PyramidGizmoActivate(editState.pyramidGizmo, inOp);
			PyramidGizmoDrag(editState.pyramidGizmo, &coneMin, &cone2Min);
			if (coneMin > coneMax)
				coneMin = coneMax;
			if (cone2Min > cone2Max)
				cone2Min = cone2Max;
		}
	}

	// modify max cone
	else if (editState.transformMode == EditMaxCone)
	{
		if (editState.trackerType == EditSpotLight)
		{
			float changed = coneMax;
			ConeGizmoDraw(editState.coneGizmo, drawMat, radiusMax, RAD(coneMax), green);
			gfxDrawCone3D(drawMat, radiusMin, RAD(coneMin), -1, paleWhite);
			if (mouseDown(MS_LEFT))
				ConeGizmoHandleInput(editState.coneGizmo, coneMax);
			ConeGizmoActivate(editState.coneGizmo, inOp);
			ConeGizmoDrag(editState.coneGizmo, &coneMax);
			if (coneMax < coneMin)
				coneMax = coneMin;
		}
		else if (editState.trackerType == EditProjectorLight)
		{
			float changed1 = coneMax, changed2 = cone2Max;
			PyramidGizmoDraw(editState.pyramidGizmo, drawMat, radiusMax, RAD(coneMax), RAD(cone2Max), green);
			gfxDrawPyramid3D(drawMat, shadowNearDist, radiusMin, RAD(coneMin), RAD(cone2Min), ColorTransparent, paleWhite);
			if (mouseDown(MS_LEFT))
				PyramidGizmoHandleInput(editState.pyramidGizmo, coneMax, cone2Max);
			PyramidGizmoActivate(editState.pyramidGizmo, inOp);
			PyramidGizmoDrag(editState.pyramidGizmo, &coneMax, &cone2Max);

			if (coneMax < coneMin)
				coneMax = coneMin;
			if (cone2Max < cone2Min)
				cone2Max = cone2Min;
		}
	}

	// TODO: this stuff needs to be put in a generic activate/deactivate callback after 
	// rewriting the lighting gizmos
	if (mouseDown(MS_LEFT) && !inOp)
	{
		inpHandled();
		tracker = wleOpPropsBegin(handle);
		if (tracker)
			inOp = true;
	}

	// apply the light properties to the def
	if (tracker && tracker->def && inOp)
	{
		groupDefAddPropertyF32(tracker->def, "LightRadiusInner", radiusMin);
		printf("updating with radius [%.3f]\n", radiusMin);
		groupDefAddPropertyF32(tracker->def, "LightRadius", radiusMax);
		if (editState.trackerType == EditSpotLight)
		{
			groupDefAddPropertyF32(tracker->def, "LightConeInner", coneMin);
			groupDefAddPropertyF32(tracker->def, "LightConeOuter", coneMax);
		}
		else if (editState.trackerType == EditProjectorLight)
		{
			groupDefAddPropertyF32(tracker->def, "LightConeInner", coneMin);
			groupDefAddPropertyF32(tracker->def, "LightConeOuter", coneMax);
			groupDefAddPropertyF32(tracker->def, "LightCone2Inner", cone2Min);
			groupDefAddPropertyF32(tracker->def, "LightCone2Outer", cone2Max);
		}
		wleOpPropsUpdate();
	}

	if (inOp && !mouseIsDown(MS_LEFT))
	{
		// end the light operation
		inpHandled();
		wleOpPropsEnd();
		inOp = false;
	}
}

/********************
* MAIN GIZMO FUNCTIONS
********************/
/******
* This function creates and initializes all of the world editor's gizmos.
******/
void wleGizmoInit(void)
{
	editState.quickPlaceState.quickRotateGizmo = RotateGizmoCreate();
	RotateGizmoSetRelativeDrag(editState.quickPlaceState.quickRotateGizmo, false);
	editState.coneGizmo = ConeGizmoCreate();
	editState.pyramidGizmo = PyramidGizmoCreate();
	editState.radGizmo = RadialGizmoCreate();
}

/******
* This function indicates whether a gizmo is active (i.e. being dragged) in the world editor.
* RETURNS:
*   bool indicating whether the user is dragging a gizmo
******/
bool wleGizmoIsActive(void)
{
	return edObjHarnessGizmoIsActive() || ConeGizmoGetActive(editState.coneGizmo)
		|| PyramidGizmoGetActive(editState.pyramidGizmo) || RadialGizmoGetActive(editState.radGizmo);
}

/******
* This function sets the various world editor gizmos' matrices to use the specified matrix.
* PARAMS:
*   mat - Mat4 to set to the gizmos
******/
void wleGizmoSetMatrix(const Mat4 mat)
{
	edObjHarnessSetGizmoMatrix(mat);
}

/******
* This function is a wrapper to the EditorObject framework that updates the selection's internal matrices and
* resets the harness's gizmo to the average of the selection's matrices.
******/
void wleGizmoUpdateMatrix(void)
{
	edObjSelectionRefreshMat(EDTYPE_TRACKER);
	edObjHarnessGizmoMatrixUpdate();
}

/******
* This function determines what to do with the various world editor gizmos.  Since most of the
* world editor gizmos are handled through the EditorObject selection harness, we are mainly dealing
* with light editing tools here.
******/
void wleGizmosOncePerFrame()
{
	if (wleSelectionGetCount() > 0)
	{
		// check relative drag mode
		if (inpLevelPeek(INP_SHIFT))
			RadialGizmoSetRelativeDrag(editState.radGizmo, false);
		else
			RadialGizmoSetRelativeDrag(editState.radGizmo, true);

		// light editing
		if (((!inpLevelPeek(INP_CONTROL) && !inpLevelPeek(INP_ALT)) || RadialGizmoGetActive(editState.radGizmo) || ConeGizmoGetActive(editState.coneGizmo) || PyramidGizmoGetActive(editState.pyramidGizmo))
			&& (editState.trackerType == EditPointLight || editState.trackerType == EditSpotLight || editState.trackerType == EditProjectorLight))
		{
			if (!(editState.transformMode == EditTranslateGizmo || editState.transformMode == EditRotateGizmo))
				handleLightInput(edObjSelectionGet(EDTYPE_TRACKER)[0]->obj);
		}
	}

	if (editState.queuedTransformMode != editState.transformMode && !wleGizmoIsActive())
		wleChangeTransformMode(editState.queuedTransformMode, editState.trackerType);

	// selection harness
	edObjHarnessOncePerFrame();
}

/********************
* GIZMO UI CALLBACKS
********************/
/******
* This function is called whenever the selector in the gizmo toolbar changes.  We use it to set the active
* gizmo.
* PARAMS:
*   gizmo - the gizmo being set to active
******/
void wleGizmosUIGizmoChanged(void *gizmo)
{
	if (gizmo == edObjHarnessGetTransGizmo())
		wleGizmosTransformModeSet(editState.trackerType, EditTranslateGizmo);
	else if (gizmo == edObjHarnessGetRotGizmo())
		wleGizmosTransformModeSet(editState.trackerType, EditRotateGizmo);
}

/*****
* This function is called when the translate gizmo is changed through the UI.  It synchronizes key attributes
* with the rotate gizmo and other linear gizmos.
* PARAMS:
*   transGizmo - TranslateGizmo changed
******/
void wleGizmosUITransGizmoChanged(TranslateGizmo *transGizmo)
{
	UIMenuItem *item;
	EditSpecialSnapMode mode = TranslateGizmoGetSpecSnap(transGizmo);

	RotateGizmoSetAlignedToWorld(edObjHarnessGetRotGizmo(), TranslateGizmoGetAlignedToWorld(transGizmo));
	RadialGizmoEnableSnap(editState.radGizmo, TranslateGizmoIsSnapEnabled(transGizmo));
	RadialGizmoSetSnapResolution(editState.radGizmo, TranslateGizmoGetSnapResolution(transGizmo));

	item = emMenuItemGet(&worldEditor, "snapgrid");
	if (item)
		item->data.state = (mode == EditSnapGrid);
	item = emMenuItemGet(&worldEditor, "snapvertex");
	if (item)
		item->data.state = (mode == EditSnapVertex);
	item = emMenuItemGet(&worldEditor, "snapmidpoint");
	if (item)
		item->data.state = (mode == EditSnapMidpoint);
	item = emMenuItemGet(&worldEditor, "snapedge");
	if (item)
		item->data.state = (mode == EditSnapEdge);
	item = emMenuItemGet(&worldEditor, "snapface");
	if (item)
		item->data.state = (mode == EditSnapFace);
	item = emMenuItemGet(&worldEditor, "snapterrain");
	if (item)
		item->data.state = (mode == EditSnapTerrain);
	item = emMenuItemGet(&worldEditor, "snapsmart");
	if (item)
		item->data.state = (mode == EditSnapSmart);
	item = emMenuItemGet(&worldEditor, "snapnone");
	if (item)
		item->data.state = (mode == EditSnapNone);
	item = emMenuItemGet(&worldEditor, "snapnormal");
	if (item)
		item->data.state = TranslateGizmoGetSnapNormal(transGizmo);
	item = emMenuItemGet(&worldEditor, "snapclamping");
	if (item)
		item->data.state = TranslateGizmoGetSnapClamp(transGizmo);

	// update preferences
	editState.inputData.transSnapEnabledKey = (TranslateGizmoGetSpecSnap(transGizmo) != EditSnapNone);
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosWorldAligned", TranslateGizmoGetAlignedToWorld(transGizmo));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnap", TranslateGizmoGetSpecSnap(transGizmo));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnapRes", TranslateGizmoGetSnapResolution(transGizmo));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnapNormal", TranslateGizmoGetSnapNormal(transGizmo));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnapNormalAxis", TranslateGizmoGetSnapNormalAxis(transGizmo));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosTransSnapNormalInv", TranslateGizmoGetSnapNormalInverse(transGizmo));
}

/*****
* This function is called when the rotate gizmo is changed through the UI.  It synchronizes key attributes
* with the translate gizmo and other angular gizmos.
* PARAMS:
*   rotGizmo - RotateGizmo changed
******/
void wleGizmosUIRotGizmoChanged(RotateGizmo *rotGizmo)
{
	TranslateGizmoSetAlignedToWorld(edObjHarnessGetTransGizmo(), RotateGizmoGetAlignedToWorld(rotGizmo));
	ConeGizmoSetSnapResolution(editState.coneGizmo, RotateGizmoGetSnapResolution(rotGizmo));
	PyramidGizmoSetSnapResolution(editState.pyramidGizmo, RotateGizmoGetSnapResolution(rotGizmo));
	ConeGizmoEnableSnap(editState.coneGizmo, RotateGizmoIsSnapEnabled(rotGizmo));
	PyramidGizmoEnableSnap(editState.pyramidGizmo, RotateGizmoIsSnapEnabled(rotGizmo));

	// update preferences
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosWorldAligned", RotateGizmoGetAlignedToWorld(rotGizmo));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosRotSnap", RotateGizmoIsSnapEnabled(rotGizmo));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "GizmosRotSnapRes", RotateGizmoGetSnapResolution(rotGizmo));
}

/******
* GIZMO SETTING/CYCLING
******/
/******
* This function indicates whether a particular transformation mode/gizmo is valid for a particular
* tracker type.
* PARAMS:
*   mode - EditTransformMode to check for validity
*   trackerType - EditTrackerType with which to check validity for mode
* RETURNS:
*   bool indicating whether mode is a valid gizmo/transformation for trackerType
******/
bool wleTransformModeIsValid(EditTransformMode mode, EditTrackerType trackerType)
{
	if (trackerType == EditTracker)
		return (mode == EditTranslateGizmo || mode == EditRotateGizmo);
	else if (trackerType == EditPointLight)
		return (mode == EditTranslateGizmo || mode == EditRotateGizmo || mode == EditScaleMin || mode == EditScale);
	else if (trackerType == EditSpotLight || trackerType == EditProjectorLight)
		return true;
	return false;
}

/******
* This function cycles through the various available world editor gizmos, at a rate of a single step
* per call of this function.  This is primarily used as the user is switching between various gizmos.
* PARAMS:
*   start - EditTransformMode indicating the initial value
*   reverse - bool indicating whether to cycle in reverse or not
* RETURNS:
*   EditTransformMode end result of the single cycle step
******/
static EditTransformMode wleIncDecTransformMode(EditTransformMode start, bool reverse)
{
	if (reverse)
		start = start - 1 + EDITTRANSFORMMODECOUNT;
	else
		start++;
	start = (start % EDITTRANSFORMMODECOUNT);

	return start;
}

/******
* This function does all that is necessary to ensure that a new transform mode is reflected throughout
* the editor.
* PARAMS:
*   mode - EditTransformMode mode to set
*   trackerType - EditTrackerType of tracker being edited; used for validating the mode being set
******/
void wleChangeTransformMode(EditTransformMode mode, EditTrackerType trackerType)
{
	editState.transformMode = editState.queuedTransformMode = mode;

	// be sure to notify the selection harness of the currently active gizmo
	if (mode == EditTranslateGizmo)
		edObjHarnessSetGizmo(EdObjTranslate);
	else if (mode == EditRotateGizmo)
		edObjHarnessSetGizmo(EdObjRotate);

	// modify UI to reflect current widget
	if (mode == EditTranslateGizmo)
		elGizmosToolbarSetActiveGizmo(editorUIState->gizmosToolbar, edObjHarnessGetTransGizmo());
	else if (mode == EditRotateGizmo)
		elGizmosToolbarSetActiveGizmo(editorUIState->gizmosToolbar, edObjHarnessGetRotGizmo());
}

/******
* This is the public function used to cycle through the various editor gizmos.
* PARAMS:
*   oldMode - EditTransformMode from which to start cycling
*   trackerType - EditTrackerType of tracker being edited, which determines available gizmos
*   backward - bool indicating whether to cycle in backwards order
* RETURNS:
*   EditTransformMode new, resulting gizmo
******/
EditTransformMode wleTransformModeCycle(EditTransformMode oldMode, EditTrackerType trackerType, bool backward)
{
	do 
	{
		oldMode = wleIncDecTransformMode(oldMode, backward);
	} while(!wleTransformModeIsValid(oldMode, trackerType));

	wleChangeTransformMode(oldMode, trackerType);

	return oldMode;
}

/******
* This can be used to set the gizmo being used directly.
* PARAMS:
*   trackerType - EditTrackerType of tracker being modified, which will restrict available gizmos
*   mode - EditTransformMode mode to use
******/
void wleGizmosTransformModeSet(EditTrackerType trackerType, EditTransformMode mode)
{
	if (wleTransformModeIsValid(mode, trackerType))
		wleChangeTransformMode(mode, trackerType);
}

#endif