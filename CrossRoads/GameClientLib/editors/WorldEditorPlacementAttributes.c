#ifndef NO_EDITORS

#include "WorldEditorPlacementAttributes.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorUtil.h"
#include "WorldEditorUI.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorOperations.h"
#include "EditorManager.h"
#include "EditorObject.h"
#include "EditLibUIUtil.h"
#include "WorldGrid.h"

#define radToDeg(rad) DEG(rad)
#define degToRad(deg) RAD(deg)

#define wleAEPlacementSetupVec3Param(param)\
	param.entry_width = 100;\
	param.entry_align = 30;\
	param.precision = 4

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/
typedef struct WleAEPlacementUI
{
	EMPanel *panel;
	UIRebuildableTree *autoWidget;
	UIScrollArea *scrollArea;

	struct 
	{
		WleAEParamVec3 position;
		WleAEParamVec3 rotation;
		WleAEParamVec3 scale;
	} data;

	bool showRotation;
	bool showScale;
} WleAEPlacementUI;

/********************
* GLOBALS
********************/
static WleAEPlacementUI wleAEGlobalPlacementUI;

/********************
* UTIL
********************/
#define wleAEPlacementUISkinAxes(param) \
	if (wleAEGlobalPlacementUI.autoWidget->root->children)\
	{\
		for (i = 0; i < 3; i++)\
		{\
			UISkin *skin = NULL;\
			switch (i)\
			{\
				xcase 0:\
					if (!param.diff[2])\
						skin = editorUIState->skinBlue;\
				xcase 1:\
					if (!param.diff[1])\
						skin = editorUIState->skinGreen;\
				xcase 2:\
					if (!param.diff[0])\
						skin = editorUIState->skinRed;\
			}\
			if (skin)\
				ui_WidgetSkin(wleAEGlobalPlacementUI.autoWidget->root->children[eaSize(&wleAEGlobalPlacementUI.autoWidget->root->children) - 1 - i]->widget1, skin);\
		}\
	}

/******
* This function rebuilds the auto widget tree, using the values currently stored in the parameters.
******/
static void wleAEPlacementRebuildUI(void)
{
	Vec3 vMin = {-1000000, -1000000, -1000000};
	Vec3 vMax = {1000000, 1000000, 1000000};
	Vec3 vStep = {0.1, 0.1, 0.1};
	int i;

	ui_RebuildableTreeInit(wleAEGlobalPlacementUI.autoWidget, &wleAEGlobalPlacementUI.scrollArea->widget.children, 0, 0, UIRTOptions_Default);
	wleAEVec3AddWidget(wleAEGlobalPlacementUI.autoWidget, "Position", "Position", "position", &wleAEGlobalPlacementUI.data.position, vMin, vMax, vStep);
	wleAEPlacementUISkinAxes(wleAEGlobalPlacementUI.data.position)
	if (wleAEGlobalPlacementUI.showRotation)
	{
		wleAEVec3AddWidget(wleAEGlobalPlacementUI.autoWidget, "Rotation", "Rotation", "rotation", &wleAEGlobalPlacementUI.data.rotation, vMin, vMax, vStep);
		wleAEPlacementUISkinAxes(wleAEGlobalPlacementUI.data.rotation)
	}
	if (wleAEGlobalPlacementUI.showScale)
	{
		wleAEVec3AddWidget(wleAEGlobalPlacementUI.autoWidget, "Scale", "Scale", "scale", &wleAEGlobalPlacementUI.data.scale, vMin, vMax, vStep);
		wleAEPlacementUISkinAxes(wleAEGlobalPlacementUI.data.scale)
	}

	ui_RebuildableTreeDoneBuilding(wleAEGlobalPlacementUI.autoWidget);
	emPanelSetHeight(wleAEGlobalPlacementUI.panel, elUIGetEndY(wleAEGlobalPlacementUI.scrollArea->widget.children[0]->children) + 20);
	wleAEGlobalPlacementUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalPlacementUI.scrollArea->widget.children[0]->children) + 5;
}

/********************
* PARAMETER CALLBACKS
********************/
// position
static void wleAEPlacementPosUpdate(WleAEParamVec3 *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		if (tracker)
		{
			Mat4 mat;
			trackerGetMat(tracker, mat);
			copyVec3(mat[3], param->vecvalue);
		}
	}
	else if (obj->type->objType == EDTYPE_PATROL_POINT)
	{
		Mat4 pointMat;
		WorldPatrolPointProperties *patrol = wlePatrolPointFromHandle(obj->obj, pointMat);
		if (patrol)
			copyVec3(pointMat[3], param->vecvalue);
	}
	else if (obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		Mat4 actorMat;
		WorldActorProperties *actor = wleEncounterActorFromHandle(obj->obj, actorMat);
		if (actor)
			copyVec3(actorMat[3], param->vecvalue);
	}
}

static void wleAEPlacementPosApply(WleAEParamVec3 *param, void *unused, EditorObject **objs)
{
	int i, j;

	TrackerHandle **trackerHandles = NULL;
	Mat4 *trackerMats = NULL;
	int trackerCount = 0;

	WleEncObjSubHandle **patrolPointSubHandles = NULL;
	Vec3 *vecs = NULL;
	int vecCount = 0, vecMax = 0;

	WleEncObjSubHandle **encounterActorSubHandles = NULL;
	Mat4 *mats = NULL;
	int matCount = 0, matMax= 0;

	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			trackerCount++;
		}
	}

	if (trackerCount)
	{
		trackerMats = calloc(trackerCount, sizeof(Mat4));
		trackerCount = 0;
	}

	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(objs[i]->obj);
			trackerGetMat(tracker, trackerMats[trackerCount]);
			for (j = 0; j < 3; j++)
			{
				if (!param->diff[j])
				{
					trackerMats[trackerCount][3][j] = param->vecvalue[j];
				}
			}

			eaPush(&trackerHandles, objs[i]->obj);
			trackerCount++;
		}
		else if (objs[i]->type->objType == EDTYPE_PATROL_POINT)
		{
			Mat4 objMat;

			eaPush(&patrolPointSubHandles, objs[i]->obj);
			dynArrayAddStruct(vecs, vecCount, vecMax);

			edObjGetMatrix(objs[i], objMat);
			copyVec3(objMat[3], vecs[vecCount-1]);
			for (j = 0; j < 3; j++)
			{
				if (!param->diff[j])
				{
					vecs[vecCount-1][j] = param->vecvalue[j];
				}
			}
		}
		else if (objs[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR)
		{
			eaPush(&encounterActorSubHandles, objs[i]->obj);
			dynArrayAddStruct(mats, matCount, matMax);

			edObjGetMatrix(objs[i], mats[matCount-1]);
			for (j = 0; j < 3; j++)
			{
				if (!param->diff[j])
				{
					mats[matCount-1][3][j] = param->vecvalue[j];
				}
			}
		}
	}

	if (trackerCount)
	{
		wleTrackersMoveToMats(trackerHandles, trackerMats, false);
		eaDestroy(&trackerHandles);
		SAFE_FREE(trackerMats);
	}

	if (vecCount)
	{
		wleOpMovePatrolPoints(patrolPointSubHandles, vecs);
		eaDestroy(&patrolPointSubHandles);
		SAFE_FREE(vecs);
	}

	if (matCount)
	{
		wleOpMoveEncounterActors(encounterActorSubHandles, mats);
		eaDestroy(&encounterActorSubHandles);
		SAFE_FREE(mats);
	}

	edObjHarnessGizmoMatrixUpdate();
}

// rotation
static void wleAEPlacementRotUpdate(WleAEParamVec3 *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);

		if (tracker)
		{
			Mat4 mat;
			Vec3 ypr;
			trackerGetMat(tracker, mat);
			getMat3YPR(mat, ypr);
			param->vecvalue[0] = radToDeg(ypr[0]);
			param->vecvalue[1] = radToDeg(ypr[1]);
			param->vecvalue[2] = radToDeg(ypr[2]);
		}
	}
	else if (obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		Mat4 actorMat;
		WorldActorProperties *actor = wleEncounterActorFromHandle(obj->obj, actorMat);
		if (actor)
		{
			Vec3 ypr;
			getMat3YPR(actorMat, ypr);
			param->vecvalue[0] = radToDeg(ypr[0]);
			param->vecvalue[1] = radToDeg(ypr[1]);
			param->vecvalue[2] = radToDeg(ypr[2]);
		}
	}
}

static void wleAEPlacementRotApply(WleAEParamVec3 *param, void *unused, EditorObject **objs)
{
	int i, j;

	TrackerHandle **trackerHandles = NULL;
	Mat4 *trackerMats = NULL;
	int trackerCount = 0;

	WleEncObjSubHandle **encounterActorSubHandles = NULL;
	Mat4 *mats = NULL;
	int matCount = 0, matMax= 0;

	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			trackerCount++;
		}
	}

	if (trackerCount)
	{
		trackerMats = calloc(trackerCount, sizeof(Mat4));
		trackerCount = 0;
	}

	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(objs[i]->obj);
			Vec3 ypr;
			trackerGetMat(tracker, trackerMats[trackerCount]);
			getMat3YPR(trackerMats[trackerCount], ypr);
			for (j = 0; j < 3; j++)
			{
				if (!param->diff[j])
				{
					ypr[j] = degToRad(param->vecvalue[j]);
				}
			}
			createMat3YPR(trackerMats[trackerCount], ypr);
			eaPush(&trackerHandles, objs[i]->obj);
			trackerCount++;
		}
		else if (objs[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR)
		{
			Vec3 ypr;
			eaPush(&encounterActorSubHandles, objs[i]->obj);
			dynArrayAddStruct(mats, matCount, matMax);

			edObjGetMatrix(objs[i], mats[matCount-1]);
			getMat3YPR(mats[0], ypr);
			for (j = 0; j < 3; j++)
			{
				if (!param->diff[j])
				{
					ypr[j] = degToRad(param->vecvalue[j]);
				}
			}
			createMat3YPR(mats[matCount-1], ypr);
		}
	}

	if (trackerCount)
	{
		wleTrackersMoveToMats(trackerHandles, trackerMats, false);
		eaDestroy(&trackerHandles);
		SAFE_FREE(trackerMats);
	}

	if (matCount)
	{
		wleOpMoveEncounterActors(encounterActorSubHandles, mats);
		eaDestroy(&encounterActorSubHandles);
		SAFE_FREE(mats);
	}

	edObjHarnessGizmoMatrixUpdate();
}

// scale
static void wleAEPlacementScaleUpdate(WleAEParamVec3 *param, void *unused, EditorObject *obj)
{
	GroupTracker *tracker;
	GroupDef *def;

	assert(obj->type->objType == EDTYPE_TRACKER);
	tracker = trackerFromTrackerHandle(obj->obj);
	def = tracker ? tracker->def : NULL;
	if (def)
		copyVec3(def->model_scale, param->vecvalue);
}

static void wleAEPlacementScaleApply(WleAEParamVec3 *param, void *unused, EditorObject **objs)
{
	int i;
	TrackerHandle **handles = NULL;
	for (i = 0; i < eaSize(&objs); i++)
	{
		assert(objs[i]->type->objType == EDTYPE_TRACKER);
		eaPush(&handles, objs[i]->obj);
	}
	wleOpSetScale(handles, &param->vecvalue);
	eaDestroy(&handles);
}

/********************
* MAIN
********************/
/******
* This function sets the position and rotation parameters to the values determined by a specified matrix.  Then
* it updates the UI to reflect that position and rotation.
* PARAMS:
*   refMat - Mat4 matrix indicating rotation and position to which the parameter values should be set
******/
void wleAEPlacementPosRotUpdate(Mat4 refMat)
{
	Vec3 ypr;

	copyVec3(refMat[3], wleAEGlobalPlacementUI.data.position.vecvalue);
	getMat3YPR(refMat, ypr);
	wleAEGlobalPlacementUI.data.rotation.vecvalue[0] = radToDeg(ypr[0]);
	wleAEGlobalPlacementUI.data.rotation.vecvalue[1] = radToDeg(ypr[1]);
	wleAEGlobalPlacementUI.data.rotation.vecvalue[2] = radToDeg(ypr[2]);
	wleAEPlacementRebuildUI();
}

/******
* This function is called when the attribute editor selection target changes or if a refresh of data
* on the current selection is prompted.
* PARAMS:
*   edObj - EditorObject whose data is being reloaded
******/
int wleAEPlacementReload(EMPanel *panel, EditorObject *edObj)
{
	EditorObject **objects = NULL;
	int i;
	bool posRotDisabled = false;
	bool scaleDisabled = false;

	wleAEGetSelectedObjects(&objects);
	wleAEGlobalPlacementUI.showRotation = true;
	wleAEGlobalPlacementUI.showScale = true;
	for (i = 0; i < eaSize(&objects); i++)
	{
		GroupTracker *tracker;

		if (objects[i]->type->objType == EDTYPE_TRACKER)
		{
			tracker = trackerFromTrackerHandle(objects[i]->obj);
			if (!tracker)
				continue;

			if (!tracker->def || !tracker->def->model)
				wleAEGlobalPlacementUI.showScale = false;
			else if (!wleTrackerIsEditable(objects[i]->obj, false, false, false))
				scaleDisabled = true;
			else if (!tracker->parent || !wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, false, true))
				posRotDisabled = true;
		}
		else
		{
			WleEncObjSubHandle *subHandle = objects[i]->obj;
			void *subObj = NULL;

			switch (objects[i]->type->objType)
			{
				xcase EDTYPE_PATROL_POINT:
					subObj = wlePatrolPointFromHandle(subHandle, NULL);
					wleAEGlobalPlacementUI.showRotation = false;
				xcase EDTYPE_ENCOUNTER_ACTOR:
					subObj = wleEncounterActorFromHandle(subHandle, NULL);
			}

			wleAEGlobalPlacementUI.showScale = false;

			if (!subObj || !wleTrackerIsEditable(subHandle->parentHandle, false, false, false))
				posRotDisabled = true;
		}
	}
	eaDestroy(&objects);

	// fill data
	wleAEGlobalPlacementUI.data.position.disabled = posRotDisabled;
	wleAEGlobalPlacementUI.data.rotation.disabled = posRotDisabled;
	wleAEGlobalPlacementUI.data.scale.disabled = scaleDisabled;

	wleAEVec3Update(&wleAEGlobalPlacementUI.data.position);
	if (wleAEGlobalPlacementUI.showRotation)
		wleAEVec3Update(&wleAEGlobalPlacementUI.data.rotation);
	if (wleAEGlobalPlacementUI.showScale)
		wleAEVec3Update(&wleAEGlobalPlacementUI.data.scale);

	// rebuild UI
	wleAEPlacementRebuildUI();

	return WLE_UI_PANEL_OWNED;
}

/******
* This function is invoked by the attribute editor to populate the contents of the attribute editor's
* panel.
* PARAMS:
*   panel - EMPanel to populate
******/
void wleAEPlacementCreate(EMPanel *panel)
{
	if (wleAEGlobalPlacementUI.autoWidget)
		return;

	wleAEGlobalPlacementUI.panel = panel;

	// initialize auto widget and scroll area
	wleAEGlobalPlacementUI.autoWidget = ui_RebuildableTreeCreate();
	wleAEGlobalPlacementUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, false);
	wleAEGlobalPlacementUI.scrollArea->widget.widthUnit = UIUnitPercentage;
	wleAEGlobalPlacementUI.scrollArea->widget.heightUnit = UIUnitPercentage;
	wleAEGlobalPlacementUI.scrollArea->widget.sb->alwaysScrollX = false;
	emPanelAddChild(panel, wleAEGlobalPlacementUI.scrollArea, false);

	// set parameter settings
	wleAEPlacementSetupVec3Param(wleAEGlobalPlacementUI.data.position);
	wleAEGlobalPlacementUI.data.position.can_copy = true;
	wleAEGlobalPlacementUI.data.position.update_func = wleAEPlacementPosUpdate;
	wleAEGlobalPlacementUI.data.position.apply_func = wleAEPlacementPosApply;
	wleAEPlacementSetupVec3Param(wleAEGlobalPlacementUI.data.rotation);
	wleAEGlobalPlacementUI.data.rotation.can_copy = true;
	wleAEGlobalPlacementUI.data.rotation.update_func = wleAEPlacementRotUpdate;
	wleAEGlobalPlacementUI.data.rotation.apply_func = wleAEPlacementRotApply;
	wleAEPlacementSetupVec3Param(wleAEGlobalPlacementUI.data.scale);
	wleAEGlobalPlacementUI.data.scale.can_copy = true;
	wleAEGlobalPlacementUI.data.scale.update_func = wleAEPlacementScaleUpdate;
	wleAEGlobalPlacementUI.data.scale.apply_func = wleAEPlacementScaleApply;
}

#endif
