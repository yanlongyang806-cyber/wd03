#include "EditorObject.h"

#ifndef NO_EDITORS
#include "EditorObjectMenus.h"
#include "EditLibGizmos.h"
#include "EditorPrefs.h"
#include "inputLib.h"
#include "partition_enums.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "wlModelInline.h"
#include "WorldColl.h"
#include "GfxPrimitive.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorAttributes.h"
#include "WorldEditorOperations.h"
#include "WorldEditorMenus.h"
#include "WorldEditorUI.h"
#include "WorldEditorOptions.h"
#include "EditorManager.h"
#include "GfxCommandParse.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* NOTES:
* Global ref count of editor objects should be:
*   # of selected objects +
*   # displayed tracker tree nodes +
*   # selection changes since last undo stack clear (ie. undo stack refs) +
*   1 if something is selected (ie. AE selection) +
*   1 if the last selected object was clicked in the viewport instead of the tracker tree (ie. lastClicked ref) +
*   1 for persistent dummy/ENTIRE SELECTION editor object
********************/

/******
* EDITOR OBJECT TYPE MANAGEMENT
******/
StashTable edObjAllTypes;
bool inside_undo = false;

typedef struct EditorObjectAction
{
	char *name;
	EdObjListCallback actionFunc;
} EditorObjectAction;

/******
* This function registers a basic editor object type.  This is a necessary step before code can use the
* editor object framework, as the object type will contain necessary function pointers to do anything
* meaningful with the editor objects in other systems.
* PARAMS:
*   type - the enumeration of the EditorObjectType
*   freeFunc - EdObjCallback function called when the EditorObject is freed
*   drawFunc - EdObjListCallback function called on the entire selection of EditorObjects once per frame
*   clickFunc - EdObjDetectCallback function used to determine whether a particular object of the
*               registered type is to be selected upon detecting a click
******/
void editorObjectTypeRegister(EditorObjectTypeEnum type, EdObjCRCCallback crcFunc, EdObjCallback freeFunc, 
							  EdObjChildrenCallback childFunc, EdObjParentCallback parentFunc,
							  EdObjListCallback drawFunc,
							  EdObjClickDetectCallback clickFunc, EdObjMarqueeDetectCallback marqueeFunc,
							  EdObjChangedCallback changedFunc, EdObjMovementEnableCallback movementEnableFunc)
{
	EditorObjectType *newType = calloc(1, sizeof(EditorObjectType));

	if (!edObjAllTypes)
		edObjAllTypes = stashTableCreateInt(10);

	newType->objType = type;
	newType->crcFunc = crcFunc;
	newType->freeFunc = freeFunc;
	newType->childFunc = childFunc;
	newType->parentFunc = parentFunc;
	newType->drawFunc = drawFunc;
	newType->clickFunc = clickFunc;
	newType->marqueeFunc = marqueeFunc;
	newType->changedFunc = changedFunc;
	newType->movementEnableFunc = movementEnableFunc;
	newType->selection = NULL;
	newType->action_table = stashTableCreateWithStringKeys(16, StashDefault);
	stashIntAddPointer(edObjAllTypes, type, newType, false);
}

/******
* This function gets an EditorObjectType give the enumerated type value.
* PARAMS:
*   type - EditorObjectTypeEnum type
* RETURNS:
*   EditorObjectType corresponding to the enumerated type
******/
EditorObjectType *editorObjectTypeGet(EditorObjectTypeEnum type)
{
	EditorObjectType *retType;
	if (type == EDTYPE_NONE)
		retType = NULL;
	else
		stashIntFindPointer(edObjAllTypes, type, &retType);
	return retType;
}

/******
* This function sets the comparison callback for the specified object type.  This comparison
* function must follow the convention of returning zero when equal and non-zero when unequal.
* For any sorting features, the comparison should also return differently signed non-zero values
* when comparisons are done in reverse order.
* PARAMS:
*   objType - EditorObjectTypeEnum type for which to set the callback
*   compareFunc - EdObjCompCallback comparison function
******/
void edObjTypeSetCompCallback(EditorObjectTypeEnum objType, EdObjCompCallback compareFunc, EdObjCompCallback compareForUIFunc)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	assert(type);
	type->compareFunc = compareFunc;
	type->compareForUIFunc = compareForUIFunc;
}

/******
* This function sets the selection and deselection callbacks for the specified object type.  This
* function is called upon the selection of an object of the specified type.
*   objType - EditorObjectTypeEnum type for which to set the callback
*   selectFunc - EdObjCallback selection function
*   deselectFunc - EdObjCallback deselection function
******/
void edObjTypeSetSelectionCallbacks(EditorObjectTypeEnum objType, EdObjCheckCallback selectFunc, EdObjCheckCallback deselectFunc, EdObjSelectionChangedCallback selectionChangedFunc)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	assert(type);
	type->selectFunc = selectFunc;
	type->deselectFunc = deselectFunc;
	type->selectionChangedFunc = selectionChangedFunc;
}

void edObjTypeSetMovementCallbacks(EditorObjectTypeEnum objType, EdObjMatCallback getMatFunc, EdObjListCallback startMoveFunc, EdObjListCallback movingFunc, EdObjListCallback endMoveFunc)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	assert(type);
	type->getMatFunc = getMatFunc;
	type->startMoveFunc = startMoveFunc;
	type->movingFunc = movingFunc;
	type->endMoveFunc = endMoveFunc;
}

void edObjTypeSetMenuCallbacks(EditorObjectTypeEnum objType, EdObjContextMenuFunc menuCreateFunc)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	assert(type);
	type->menuFunc = menuCreateFunc;
}

void edObjTypeSetBoundsCallback(EditorObjectTypeEnum objType, EdObjBoundsCallback getBoundsFunc)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	assert(type);
	type->getBoundsFunc = getBoundsFunc;
}

void edObjTypeActionRegister(EditorObjectTypeEnum objType, const char *action_name, EdObjListCallback actionFunc)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	EditorObjectAction *action = (EditorObjectAction*)calloc(1, sizeof(EditorObjectAction));
	assert(type);
	
	action->name = strdup(action_name);
	action->actionFunc = actionFunc;
	stashAddPointer(type->action_table, action->name, action, true);
}

void editorObjectActionDispatch(const char *action_name)
{
	StashTableIterator iter;
	StashElement el;
	EditorObjectAction *action;

	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		EditorObjectType *type = stashElementGetPointer(el);
		assert(type);
		if (stashFindPointer(type->action_table, action_name, &action))
		{
			int i;
			EditorObject **obj_list = NULL;
			eaCopy(&obj_list, &type->selection);
			for (i = 0; i < eaSize(&obj_list); i++)
				editorObjectRef(obj_list[i]);
			action->actionFunc(obj_list);
			eaDestroyEx(&obj_list, editorObjectDeref);
		}
	}
}


/******
* EDITOR OBJECT MANAGEMENT
******/
/******
* This function creates a new EditorObject.
* PARAMS:
*   obj - the data being held in the EditorObject
*   context - contextual data associated with the EditorObject (this data will never be touched
*             by the framework)
*   objType - EditorObjectTypeEnum type of EditorObject
* RETURNS:
*   EditorObject created object
******/
EditorObject *editorObjectCreate(void *obj, const char *name, void *context, EditorObjectTypeEnum objType)
{
	// TODO: allocate this struct on a memory pool
	EditorObject *edObj = calloc(1, sizeof(EditorObject));
	edObj->name = strdup(name ? name : "");
	edObj->obj = obj;
	edObj->context = context;
	stashIntFindPointer(edObjAllTypes, objType, &edObj->type);
	assert(edObj->type);
	if (edObj->type->getMatFunc)
		edObj->type->getMatFunc(edObj, edObj->mat);
	else
		copyMat4(unitmat, edObj->mat);
	copyMat4(edObj->mat, edObj->oldMat);
	return edObj;
}

static int globRef = 0;
/******
* This function adds one to the reference count of the specified EditorObject - this should be called
* whenever a pointer to the object is stored.  The reference count counts how many places the object
* is being used.
* PARAMS:
*   edObj - EditorObject to reference
******/
void editorObjectRef(EditorObject *edObj)
{
	edObj->ref_count++;
	globRef++;
	//printf("Ref Object %s - %d [glob=%i]\n", edObj->name, edObj->ref_count, globRef);
}

/******
* This function subtracts one from the reference count of the specified EditorObject - this should be
* called whenever a pointer to this object is cleared.  When the object's reference count reaches zero,
* it is freed.
* PARAMS:
*   edObj - EditorObject to dereference
******/
void editorObjectDeref(EditorObject *edObj)
{
	edObj->ref_count--;
	globRef--;
	if (edObj->ref_count <= 0)
	{
		//printf("Der Object %s - Free [glob=%i]\n", edObj->name, globRef);
		if (edObj->type->freeFunc)
			edObj->type->freeFunc(edObj);

		// TODO: free this from memory pool
		SAFE_FREE(edObj->name);
		free(edObj);
	}
	else
	{
		//printf("Der Object %s - %d [glob=%i]\n", edObj->name, edObj->ref_count, globRef);
	}
}

/******
* EDITOR OBJECT SELECTION HARNESS - SELECTION
******/
#define HARNESS_MARQ_MOUSE_DIFF_SCALE 1.0f
#define HARNESS_MARQ_DIST_MIN 10
#define HARNESS_MARQ_DIST_MAX 5000
#define HARNESS_MARQ_DEFAULT_DIST 1000

typedef struct EditorObjectHarness
{
	// state
	EditorObject *lastClicked;
	int quickPlaceTimer;

	// gizmos
	TranslateGizmo *transGizmo;
	RotateGizmo *rotGizmo;
	ScaleMinMaxGizmo *scaleMinMaxGizmo;

	EditorObjectPivotMode pivotMode;
	EditorObjectGizmoMode gizmoMode;
	bool showGizmos;
	bool movementEnabled;
	bool selectionDisabled;
	bool volumeSelected;

	Mat4 oldMat, mat;
	int marqueeStartX, marqueeStartY, marqueeEndX, marqueeEndY;
	float marqueeDist;
	bool marqueeDrag, marqueeDragDist;
	bool crossingMode;

	// undo stack
	EditUndoStack *edObjUndoStack;
} EditorObjectHarness;

static EditorObjectHarness edObjGlobalHarness;

/******
* This function compares two EditorObjects, returning zero if equal and a non-zero integer if not
* equal.  If the two objects are not of the same type or their type does not have a comparator, the
* editor object's data pointers are compared.
* PARAMS:
*   edObj1 - EditorObject to compare
*   edObj2 - EditorObject to compare
* RETURNS:
*   int zero if equal, non-zero if unequal
******/
int edObjCompare(EditorObject *edObj1, EditorObject *edObj2)
{
	int ret = (int) ((intptr_t) edObj1->obj - (intptr_t) edObj2->obj);
	editorObjectRef(edObj1);
	editorObjectRef(edObj2);
	if (edObj1 == edObj2 || edObj1->obj == edObj2->obj)
		ret = 0;
	else if (edObj1->type && edObj2->type && edObj1->type == edObj2->type && edObj1->type->compareFunc)
		ret = edObj1->type->compareFunc(edObj1, edObj2);
	editorObjectDeref(edObj1);
	editorObjectDeref(edObj2);
	return ret;
}

/******
* This function compares two EditorObjects exactly as edObjCompare does, except that it uses the type's
* compareForUI function.
* PARAMS:
*   edObj1 - EditorObject to compare
*   edObj2 - EditorObject to compare
* RETURNS:
*   int zero if equal, non-zero if unequal
******/
int edObjCompareForUI(EditorObject *edObj1, EditorObject *edObj2)
{
	int ret = (int) ((intptr_t) edObj1->obj - (intptr_t) edObj2->obj);
	editorObjectRef(edObj1);
	editorObjectRef(edObj2);
	if (edObj1 == edObj2 || edObj1->obj == edObj2->obj)
		ret = 0;
	else if (edObj1->type && edObj2->type && edObj1->type == edObj2->type && edObj1->type->compareForUIFunc)
		ret = edObj1->type->compareForUIFunc(edObj1, edObj2);
	editorObjectDeref(edObj1);
	editorObjectDeref(edObj2);
	return ret;
}

/******
* This function looks for a particular EditorObject in the selection list and returns the matching
* object in the list (or NULL if no match is found).
* PARAMS:
*   edObj - EditorObject to look for
* RETURNS:
*   EditorObject stored in the selection list matching edObj; NULL if no match was found
******/
EditorObject *edObjFindSelected(EditorObject *edObj)
{
	EditorObject *ret = NULL;
	int i;

	assert(edObj->type);
	editorObjectRef(edObj);
	for (i = 0; i < eaSize(&edObj->type->selection); i++)
	{
		if (edObjCompare(edObj, edObj->type->selection[i]) == 0)
			ret = edObj->type->selection[i];
	}
	editorObjectDeref(edObj);
	return ret;
}

void edObjAERefresh()
{
	EditorObject **selection_list = NULL;
	edObjSelectionGetAll(&selection_list);
	wleUIRefreshEditingSelector(selection_list);
	eaDestroy(&selection_list);
}

static void edObjRecursiveDeselectChildren(EditorObject *edObj)
{
	int i;
	EditorObject **children = NULL;
	if (edObj->type->childFunc)
	{
		edObj->type->childFunc(edObj, &children);
		for (i = 0; i < eaSize(&children); i++)
		{
			editorObjectRef(children[i]);
			edObjRecursiveDeselectChildren(children[i]);
		}
		//Pulling out deselection into a list reduces calls & saves a lot of time.
		edObjDeselectListEx(children, false);
		eaDestroyEx(&children, editorObjectDeref);
	}
}

static void edObjDeselectParents(EditorObject *edObj)
{
	EditorObject *object;
	if (edObj->type->parentFunc && (object = edObj->type->parentFunc(edObj)))
	{
		ANALYSIS_ASSUME(object != NULL);
		editorObjectRef(object);
		edObjDeselect(object);
		edObjDeselectParents(object);
		editorObjectDeref(object);
	}
}

static void edObjUpdateVolumeGizmoState()
{
	if (edObjSelectionGetCount(EDTYPE_NONE) == 1)
	{
		int i;
		EditorObject **selection = edObjSelectionGet(EDTYPE_TRACKER);
		for ( i=0; i < eaSize(&selection); i++ )
		{
			GroupTracker *tracker = trackerFromTrackerHandle(selection[i]->obj);
			if(tracker && tracker->def->property_structs.volume)
			{
				if(tracker->def->property_structs.volume->eShape == GVS_Box) {
					ScaleMinMaxGizmoSetMinMax(edObjGlobalHarness.scaleMinMaxGizmo, tracker->def->property_structs.volume->vBoxMin, tracker->def->property_structs.volume->vBoxMax);
					ScaleMinMaxGizmoSetMirrored(edObjGlobalHarness.scaleMinMaxGizmo, false);
					edObjGlobalHarness.volumeSelected = true;
					return;
				} else if(tracker->def->property_structs.volume->eShape == GVS_Sphere) {
					Vec3 scaleMinMax[2];
					setVec3same(scaleMinMax[0], -tracker->def->property_structs.volume->fSphereRadius);
					setVec3same(scaleMinMax[1],  tracker->def->property_structs.volume->fSphereRadius);
					ScaleMinMaxGizmoSetMinMax(edObjGlobalHarness.scaleMinMaxGizmo, scaleMinMax[0], scaleMinMax[1]);
					ScaleMinMaxGizmoSetMirrored(edObjGlobalHarness.scaleMinMaxGizmo, true);
					edObjGlobalHarness.volumeSelected = true;
					return;
				}
			}
		}
	}
	edObjGlobalHarness.volumeSelected = false;
}

/******
* This function selects a particular EditorObject, adding it to the selection list and optionally expanding the
* tracker tree to its corresponding node.
* PARAMS:
*   edObj - EditorObject to select
*   additive - bool indicating whether to add the specified object to the selection (true) or clear the entire
*              selection first before adding it (false)
*   expandToNode - bool indicating whether to expand to the tracker tree node or not
* RETURNS:
*   bool indicating whether object was selected
******/
static bool g_selectingEdObjs = false;	//This variable is used for skipping refresh code that only needs to be called once in a select or deselect operation. Refresh has n! time and is the biggest slowdown in the selection pipeline.
//#define DEBUG_SELECTION
bool edObjSelect(EditorObject *edObj, bool additive, bool expandToNode)
{
	bool ret = false;
	bool finishUp = !g_selectingEdObjs;

#ifdef DEBUG_SELECTION
	printf("Select Start\n");
#endif

	g_selectingEdObjs = true;

	assert(edObj->type);
	editorObjectRef(edObj);

	// highlight existing selection
	if (edObjFindSelected(edObj))
	{
		wleUITrackerTreeHighlightEdObj(edObj);
		wleAESetActiveQueued(edObj);
	}

	// perform selection
	else if (!edObj->type->selectFunc || edObj->type->selectFunc(edObj))
	{
#ifdef DEBUG_SELECTION
		printf("Acting on: %s\n", edObj->name);
#endif

		editorObjectRef(edObj);

		if (!inside_undo)
			EditUndoBeginGroup(edObjGlobalHarness.edObjUndoStack);

		if (!additive)
		{
			EditorObject **oldSelection = NULL;

			edObjSelectionGetAll(&oldSelection);
			eaForEach(&oldSelection, editorObjectRef);
			edObjDeselectListEx(oldSelection, false);
			eaDestroyEx(&oldSelection, editorObjectDeref);
		}
		else
		{
			edObjDeselect(editorObjectCreate(NULL, "FXTargetNode", NULL, EDTYPE_FX_TARGET_NODE));
			edObjRecursiveDeselectChildren(edObj);
			edObjDeselectParents(edObj);
		}

		eaPush(&edObj->type->selection, edObj);
		if (expandToNode)
		{
			wleUITrackerTreeExpandToEdObj(edObj);
		}
		ret = true;

		if (edObj->type->selectionChangedFunc)
		{
			edObj->type->selectionChangedFunc(edObj->type->selection, inside_undo);
		}

		if (!inside_undo)
		{
			EditorObject **newSelection = NULL;
			eaPush(&newSelection, edObj);
			EditCreateUndoSelect(edObjGlobalHarness.edObjUndoStack, newSelection, NULL);
			eaDestroy(&newSelection);
			EditUndoEndGroup(edObjGlobalHarness.edObjUndoStack);
		}

		if (finishUp)
		{
			// set the harness gizmos up
			edObjRefreshMat(edObj);
			if (edObjSelectionGetCount(EDTYPE_NONE) > 0)
			{
				if (edObjSelectionGetCount(EDTYPE_NONE) == 1 || edObjGlobalHarness.pivotMode != EdObjEditPivot)
				{
					edObjHarnessGizmoMatrixUpdate();
				}
				edObjUpdateVolumeGizmoState();
				edObjGlobalHarness.showGizmos = true;
			}

			if (edObjSelectionGetCount(EDTYPE_NONE) == 1)
				gfxScreenshotName(edObj->name);
			else
				gfxScreenshotName("");

			edObjAERefresh();
			wleAESetActiveQueued(edObj);
			wleUITrackerTreeHighlightEdObj(edObj);
			ui_ColorWindowOrphanAll();
		}
	}
	editorObjectDeref(edObj);

	// this should get reset - is only relevant during in-world click selection
	if (edObjGlobalHarness.lastClicked)
	{
		editorObjectDeref(edObjGlobalHarness.lastClicked);
		edObjGlobalHarness.lastClicked = NULL;
	}
	if (finishUp)
	{
		g_selectingEdObjs = false;
	}

#ifdef DEBUG_SELECTION
	printf("Select End\n");
#endif

	return ret;
}

/******
* This function selects a particular EditorObject, adding it to the selection list and optionally expanding the
* tracker tree to its corresponding node.
* PARAMS:
*   edObj - EditorObject to select
*   additive - bool indicating whether to add the specified object to the selection (true) or clear the entire
*              selection first before adding it (false)
*   expandToNode - bool indicating whether to expand to the tracker tree node or not
* RETURNS:
*   bool indicating whether object was selected
******/
bool edObjSelectList(EditorObject **edObjs, bool additive, bool expandToNode)
{
	bool ret = false;
	bool beganUndo = false;
	U32 *callCallbacks = NULL;
	int i, j;

	if (eaSize(&edObjs) == 1)
	{
		return edObjSelect(edObjs[0], additive, expandToNode);
	}

#ifdef DEBUG_SELECTION
	printf("Select List Start (%d)\n", eaSize(&edObjs));
#endif

	g_selectingEdObjs = true;

	if (!additive)
	{
		EditorObject **oldSelection = NULL, **deselect_list = NULL;
		edObjSelectionGetAll(&oldSelection);
		eaCopy(&deselect_list, &oldSelection);
		eaForEach(&deselect_list, editorObjectRef);
		edObjDeselectListEx(deselect_list, false);
		eaDestroy(&deselect_list);
		eaDestroyEx(&oldSelection, editorObjectDeref);
	}

	for (i = 0; i < eaSize(&edObjs); i++)
	{
		assert(edObjs[i]->type);
		editorObjectRef(edObjs[i]);

		// highlight existing selection
		if ((additive) && (edObjFindSelected(edObjs[i])))
		{
			wleUITrackerTreeHighlightEdObj(edObjs[i]);
			wleAESetActiveQueued(edObjs[i]);
		}

		// perform selection
		else if (!edObjs[i]->type->selectFunc || edObjs[i]->type->selectFunc(edObjs[i]))
		{
#ifdef DEBUG_SELECTION
			printf("Acting on: %s\n", edObjs[i]->name);
#endif

			editorObjectRef(edObjs[i]);

			if (!beganUndo && !inside_undo)
			{
				EditUndoBeginGroup(edObjGlobalHarness.edObjUndoStack);
				beganUndo = true;
			}

			if (additive)
			{
				edObjDeselect(editorObjectCreate(NULL, "FXTargetNode", NULL, EDTYPE_FX_TARGET_NODE));
				edObjRecursiveDeselectChildren(edObjs[i]);
				edObjDeselectParents(edObjs[i]);
			}

			eaPush(&edObjs[i]->type->selection, edObjs[i]);
			ret = true;

			//The callback only needs to be called once per type.
			if (edObjs[i]->type->selectionChangedFunc)
			{
				bool push = true;
				for (j = 0; j < ea32Size(&callCallbacks); j++)
				{
					if (edObjs[callCallbacks[j]]->type == edObjs[i]->type)
					{
						push = false;
						break;
					}
				}
				if (push)
				{
					ea32Push(&callCallbacks, i);
				}
			}
		}
	}

	if (ret && expandToNode)
	{
		wleUITrackerTreeExpandToEdObj(edObjs[eaSize(&edObjs) - 1]);
	}

	for (i = 0; i < ea32Size(&callCallbacks); i++)
	{
		edObjs[callCallbacks[i]]->type->selectionChangedFunc(edObjs[callCallbacks[i]]->type->selection, inside_undo);
	}
	ea32Destroy(&callCallbacks);

	if (ret)
	{
		if (!inside_undo)
		{
			EditCreateUndoSelect(edObjGlobalHarness.edObjUndoStack, edObjs, NULL);
		}

		if (beganUndo)
		{
			EditUndoEndGroup(edObjGlobalHarness.edObjUndoStack);
		}

		// set the harness gizmos up
		for (i = 0; i < eaSize(&edObjs); i++)
		{
			edObjRefreshMat(edObjs[i]);
		}
		if (edObjSelectionGetCount(EDTYPE_NONE) > 0)
		{
			if (edObjSelectionGetCount(EDTYPE_NONE) == 1 || edObjGlobalHarness.pivotMode != EdObjEditPivot)
				edObjHarnessGizmoMatrixUpdate();
			edObjUpdateVolumeGizmoState();
			edObjGlobalHarness.showGizmos = true;
		}

		if (edObjSelectionGetCount(EDTYPE_NONE) == 1)
			gfxScreenshotName(edObjs[0]->name);
		else
			gfxScreenshotName("");

		edObjAERefresh();
		for (i = 0; i < eaSize(&edObjs); i++)
		{
			wleAESetActiveQueued(edObjs[i]);
			wleUITrackerTreeHighlightEdObj(edObjs[i]);
		}
	}

	for (i = 0; i < eaSize(&edObjs); i++)
	{
		editorObjectDeref(edObjs[i]);
	}

	// this should get reset - is only relevant during in-world click selection
	if (edObjGlobalHarness.lastClicked)
	{
		editorObjectDeref(edObjGlobalHarness.lastClicked);
		edObjGlobalHarness.lastClicked = NULL;
	}
	g_selectingEdObjs = false;
	ui_ColorWindowOrphanAll();

#ifdef DEBUG_SELECTION
	printf("Select List End\n");
#endif

	return ret;
}

/******
* This function deselects a particular EditorObject, removing its matching entry from the selection list.
* PARAMS:
*   edObj - EditorObject to deselect
* RETURNS:
*   bool indicating whether object was deselected
******/
bool edObjDeselect(EditorObject *edObj)
{
	EditorObject **beforeArray = NULL;
	EditorObject *foundObj;
	bool ret = false;
	assert(edObj->type);

#ifdef DEBUG_SELECTION
	printf("Deselect Start\n");
#endif

	edObjMenuHide(wleMenuRightClick);

	editorObjectRef(edObj);
	while ((foundObj = edObjFindSelected(edObj)) && (!foundObj->type->deselectFunc || foundObj->type->deselectFunc(foundObj)))
	{
#ifdef DEBUG_SELECTION
		printf("Acting on: %s\n", edObj->name);
#endif

		ANALYSIS_ASSUME(foundObj);
		eaFindAndRemove(&edObj->type->selection, foundObj);
		editorObjectDeref(foundObj);

		if (edObj->type->selectionChangedFunc)
			edObj->type->selectionChangedFunc(edObj->type->selection, false);

		if (!inside_undo)
		{
			eaPush(&beforeArray, edObj);
			EditCreateUndoSelect(edObjGlobalHarness.edObjUndoStack, NULL, beforeArray);
			eaDestroy(&beforeArray);
		}

		ret = true;
	}
	if (ret)
	{
		edObjFinishUp();
	}
	editorObjectDeref(edObj);

#ifdef DEBUG_SELECTION
	printf("Deselect End\n");
#endif

	return ret;
}

//This is essentially the finishing code of deselection. It's in a separate function to facilitate it's call being dependant on whether or not select is also called, as the finish up code only needs to run once in an operation.
void edObjFinishUp()
{
	if (g_selectingEdObjs) return;

	edObjHarnessGizmoMatrixUpdate();
	if (edObjSelectionGetCount(EDTYPE_NONE) == 0)
	{
		edObjGlobalHarness.showGizmos = false;
	}

	if (edObjSelectionGetCount(EDTYPE_NONE) == 1)
	{
		EditorObject **selection = NULL;
		edObjSelectionGetAll(&selection);
		gfxScreenshotName(selection[0]->name);
		eaDestroy(&selection);
	}
	else
		gfxScreenshotName("");

	edObjUpdateVolumeGizmoState();
	edObjAERefresh();
	ui_ColorWindowOrphanAll();
}

/******
* This function deselects a list of EditorObjects, removing its matching entries from the selection list.
* PARAMS:
*   edObjs - earray of EditorObjects to deselect (Must NOT be the same array as any type->selection!)
* RETURNS:
*   bool indicating whether objects were deselected
******/
bool edObjDeselectListEx(EditorObject **edObjs, bool finishUp)
{
	EditorObject **beforeArray = NULL;
	bool ret = false;
	bool falsifyGlobal = false;
	int i;
	U32 *callCallbacks = NULL;

#ifdef DEBUG_SELECTION
	printf("Deselect List Start (%d)\n", eaSize(&edObjs));
#endif

	if (g_selectingEdObjs)
	{
		finishUp = false;
	}
	else if (!finishUp)
	{
		g_selectingEdObjs = true;
		falsifyGlobal = true;
	}

	edObjMenuHide(wleMenuRightClick);

	for (i = 0; i < eaSize(&edObjs); i++)
	{
		EditorObject *foundObj;
		editorObjectRef(edObjs[i]);
		foundObj = edObjFindSelected(edObjs[i]);
		if ((foundObj) && (!foundObj->type->deselectFunc || foundObj->type->deselectFunc(foundObj)))
		{
			assert(foundObj->type);
			ANALYSIS_ASSUME(foundObj);
			if (eaFindAndRemove(&edObjs[i]->type->selection, foundObj) == -1)
			{
				continue;
			}
			editorObjectDeref(foundObj);

#ifdef DEBUG_SELECTION
			printf("Acting on: %s\n", edObjs[i]->name);
#endif

			//The callback only needs to be called once per type, after the fact.
			if (edObjs[i]->type->selectionChangedFunc)
			{
				bool push = true;
				int j;
				for (j = 0; j < ea32Size(&callCallbacks); j++)
				{
					if (edObjs[callCallbacks[j]]->type == edObjs[i]->type)
					{
						push = false;
						break;
					}
				}
				if (push)
				{
					ea32Push(&callCallbacks, i);
				}
			}

			if (!inside_undo)
			{
				eaPush(&beforeArray, edObjs[i]);
				EditCreateUndoSelect(edObjGlobalHarness.edObjUndoStack, NULL, beforeArray);
				eaDestroy(&beforeArray);
			}
			ret = true;
		}
	}

	for (i = 0; i < ea32Size(&callCallbacks); i++)
	{
		edObjs[callCallbacks[i]]->type->selectionChangedFunc(edObjs[callCallbacks[i]]->type->selection, false);
	}
	ea32Destroy(&callCallbacks);

	if (ret && finishUp)
	{
		edObjFinishUp();
	}

	for (i = 0; i < eaSize(&edObjs); i++)
	{
		editorObjectDeref(edObjs[i]);
	}

	if (falsifyGlobal)
	{
		g_selectingEdObjs = false;
	}

#ifdef DEBUG_SELECTION
	printf("Deselect List End\n");
#endif

	return ret;
}

/******
* This function toggles the selection state of the specified editor object
* PARAMS:
*   edObj - EditorObject to toggle
* RETURNS:
*   bool indicating final selected state of edObj (true if selected, false otherwise)
******/
bool edObjSelectToggle(EditorObject *edObj)
{
	bool ret;
	editorObjectRef(edObj);
	if (!edObjFindSelected(edObj))
		ret = edObjSelect(edObj, true, true);
	else
		ret = edObjDeselect(edObj);
	editorObjectDeref(edObj);
	return ret;
}

/******
* This performs a downward traversal of the current selection, depending on what was last clicked or selected.
******/
void edObjSelectDownTree(void)
{
	EditorObject *focalPoint;
	bool resetLastClicked = !!edObjGlobalHarness.lastClicked;

	if (edObjGlobalHarness.lastClicked)
		focalPoint = edObjGlobalHarness.lastClicked;
	else
		focalPoint = wleUITrackerTreeGetSelectedEdObj();

	if (focalPoint)
	{
		EditorObject *parent = focalPoint;
		EditorObject *lastChild = focalPoint;
		bool selected = false;

		editorObjectRef(focalPoint);
		editorObjectRef(parent);
		editorObjectRef(lastChild);
		do 
		{
			ANALYSIS_ASSUME(parent != NULL);
			if (edObjFindSelected(parent) && edObjCompare(lastChild, parent) != 0)
			{
				edObjSelect(lastChild, true, true);
				wleUITrackerTreeCenterOnEdObj(lastChild);
				selected = true;
			}
			else if (parent->type->parentFunc)
			{
				EditorObject *oldParent = parent;

				// set last traversed child
				editorObjectDeref(lastChild);
				lastChild = parent;
				editorObjectRef(lastChild);

				// set parent
				parent = parent->type->parentFunc(parent);
				editorObjectDeref(oldParent);
				if (parent)
					editorObjectRef(parent);
			}
			else
			{
				editorObjectDeref(parent);
				parent = NULL;
			}
		} while(parent && !selected);
		if (parent)
			editorObjectDeref(parent);
		editorObjectDeref(lastChild);

		if (!selected && focalPoint->type->childFunc)
		{
			EditorObject **children = NULL;
			focalPoint->type->childFunc(focalPoint, &children);
			eaForEach(&children, editorObjectRef);
			if (eaSize(&children) > 0)
			{
				edObjSelect(children[0], true, true);
				wleUITrackerTreeCenterOnEdObj(children[0]);
			}
			eaDestroyEx(&children, editorObjectDeref);
		}

		if (resetLastClicked && !edObjGlobalHarness.lastClicked && selected)
		{
			edObjGlobalHarness.lastClicked = focalPoint;
			editorObjectRef(edObjGlobalHarness.lastClicked);
		}
		editorObjectDeref(focalPoint);
	}
}

/******
* This performs an upward traversal of the current selection, depending on what was last clicked or selected.
******/
void edObjSelectUpTree(void)
{
	EditorObject *focalPoint;
	bool resetLastClicked;

	if (edObjGlobalHarness.lastClicked)
		focalPoint = edObjGlobalHarness.lastClicked;
	else
	{
		focalPoint = wleUITrackerTreeGetSelectedEdObj();
		if (focalPoint)
		{
			edObjGlobalHarness.lastClicked = focalPoint;
			editorObjectRef(edObjGlobalHarness.lastClicked);
		}
	}

	resetLastClicked = !!edObjGlobalHarness.lastClicked;
	if (focalPoint)
	{
		EditorObject *temp = focalPoint;
		bool selected = false;

		editorObjectRef(focalPoint);
		editorObjectRef(temp);
		do 
		{
			if (temp->type->parentFunc)
			{
				EditorObject *parent = temp->type->parentFunc(temp);
				if (parent)
				{
					editorObjectRef(parent);
					if (edObjFindSelected(temp))
					{
						edObjSelect(parent, true, true);
						wleUITrackerTreeCenterOnEdObj(parent);
						selected = true;
					}
					else
					{
						editorObjectDeref(temp);
						temp = parent;
						editorObjectRef(temp);
					}
					editorObjectDeref(parent);
				}
				else
				{
					editorObjectDeref(temp);
					temp = NULL;
				}
			}
			else
			{
				editorObjectDeref(temp);
				temp = NULL;
			}
		} while(temp && !selected);
		if (temp)
			editorObjectDeref(temp);

		if (resetLastClicked && !edObjGlobalHarness.lastClicked)
		{
			edObjGlobalHarness.lastClicked = focalPoint;
			editorObjectRef(edObjGlobalHarness.lastClicked);
		}
		editorObjectDeref(focalPoint);
	}
}

/******
* This function populates a list with all EditorObjects in the selection list that match a specified type.
* Do NOT pass in EDTYPE_NONE, as this will assert.  The selection EArray returned by this function is
* read-only - editing its contents will cause unexpected behavior.
* PARAMS:
*   objType - EditorObjectTypeEnum type whose selection should be returned
* RETURNS:
*   EditorObject EArray (READ-ONLY!) of the currently selected objects
******/
EditorObject **edObjSelectionGet(EditorObjectTypeEnum objType)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	assert(type);
	return type->selection;
}

/******
* This function populates an EArray with ALL selected editor objects across all types.
* PARAMS:
*   select - EditorObject EArray handle that will be populated with entire objects selection
******/
void edObjSelectionGetAll(EditorObject ***selection)
{
	StashTableIterator iter;
	StashElement el;

	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		EditorObjectType *type = stashElementGetPointer(el);
		assert(type);
		eaPushEArray(selection, &type->selection);
	}
}

/******
* This function returns the number of objects of a particular type currently selected.  Passing
* EDTYPE_NONE will return the total selection count across all types.
* PARAMS:
*   objType - EditorObjectTypeEnum type whose selection will be counted; EDTYPE_NONE will count
*             selection across all types
* RETURNS:
*   int number of objects of objType currently selected
******/
int edObjSelectionGetCount(EditorObjectTypeEnum objType)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	if (!type)
	{
		StashTableIterator iter;
		StashElement el;
		int count = 0;

		stashGetIterator(edObjAllTypes, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			type = stashElementGetPointer(el);
			assert(type);
			count += eaSize(&type->selection);
		}

		return count;
	}
	else
		return eaSize(&type->selection);
}

/******
* This function clears all EditorObjects in the selection list that match a specified type; if the specified
* type is EDTYPE_NONE, all objects are cleared from the selection.
* PARAMS:
*   objType - EditorObjectTypeEnum type to use as a filter; EDTYPE_NONE will clear all results
******/
void edObjSelectionClearEx(EditorObjectTypeEnum objType, bool finishUp)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	EditorObject **deselection_list = NULL;

	EditUndoBeginGroup(edObjGlobalHarness.edObjUndoStack);
	if (type)
	{
		eaCopy(&deselection_list, &type->selection);
		edObjDeselectListEx(deselection_list, finishUp);
		eaDestroy(&deselection_list);
	}
	else if (objType == EDTYPE_NONE)
	{
		// clear ENTIRE selection across all types
		StashTableIterator iter;
		StashElement el;

		stashGetIterator(edObjAllTypes, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			type = stashElementGetPointer(el);
			assert(type);

			eaCopy(&deselection_list, &type->selection);
			edObjDeselectListEx(deselection_list, finishUp);
			eaDestroy(&deselection_list);
		}
	}
	EditUndoEndGroup(edObjGlobalHarness.edObjUndoStack);
}

/******
* EDITOR OBJECT SELECTION HARNESS - MOVING
******/
static void edObjHarnessActivate(const Mat4 mat, void *unused);
void edObjHarnessDeactivate(const Mat4 mat, void *unused);

/******
* This function adjusts the matrices of the selected EditorObjects, applying an offset calculated
* as the transformation from the current gizmo matrix to the specified matrix.
* PARAMS:
*   mat - Mat4 to apply to the selection
******/
void edObjSelectionMoveToMat(const Mat4 mat)
{
	StashTableIterator iter;
	StashElement el;
	Mat4 tempMat, offsetMat;
	EditorObjectType *type;

	// find the transformation (offset) to apply to the selection
	invertMat4(edObjGlobalHarness.oldMat, tempMat);
	mulMat4(mat, tempMat, offsetMat);

	// apply the offset and call the moving function on the selection for each type
	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		int i;

		type = stashElementGetPointer(el);
		assert(type);
		for (i = 0; i < eaSize(&type->selection); i++)
		{
			mulMat4(offsetMat, type->selection[i]->oldMat, tempMat);
			copyMat4(tempMat, type->selection[i]->mat);
		}
		if (type->movingFunc && eaSize(&type->selection) > 0)
			type->movingFunc(type->selection);
	}
	copyMat4(mat, edObjGlobalHarness.mat);
}

void edObjUndoTransformFunction(void *context, const Mat4 mat)
{
	StashTableIterator iter;
	StashElement el;
	Mat4 tempMat;
	EditorObjectType *type;

	// apply the offset and call the moving function on the selection for each type
	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		int i;

		type = stashElementGetPointer(el);
		assert(type);

		if (type->startMoveFunc && eaSize(&type->selection) > 0)
			type->startMoveFunc(type->selection);

		for (i = 0; i < eaSize(&type->selection); i++)
		{
			if (type->getMatFunc)
				type->getMatFunc(type->selection[i], type->selection[i]->mat);
			mulMat4(mat, type->selection[i]->mat, tempMat);
			copyMat4(tempMat, type->selection[i]->mat);
		}
		if (type->movingFunc && eaSize(&type->selection) > 0)
		{
			type->movingFunc(type->selection);
			type->endMoveFunc(type->selection);
		}
	}

	// update the gizmo matrices
	mulMat4(mat, edObjGlobalHarness.mat, tempMat);
	edObjHarnessSetGizmoMatrix(tempMat);
}

/******
* This function gets the matrix of an EditorObject using the type's matrix function.
* PARAMS:
*   edObj - EditorObject whose matrix will be retrieved
*   outMat - Mat4 matrix output
******/
void edObjGetMatrix(EditorObject *edObj, Mat4 outMat)
{
	if (edObj->type->getMatFunc)
		edObj->type->getMatFunc(edObj, outMat);
}

/******
* This function sets the matrix on an EditorObject.  This internal matrix is manipulated by the
* translation and rotation gizmos.
* PARAMS:
*   edObj - EditorObject whose matrix will be set
*   mat - Mat4 matrix being applied
******/
void edObjSetMatrix(EditorObject *edObj, const Mat4 mat)
{
	copyMat4(mat, edObj->mat);
	copyMat4(mat, edObj->oldMat);
}

/******
* This function refreshes and EditorObject's matrix using its types getMatFunc callback, if specified.
* PARAMS:
*   edObj - EditorObject to refresh
******/
void edObjRefreshMat(EditorObject *edObj)
{
	assert(edObj->type);
	if (edObj->type->getMatFunc)
	{
		edObj->type->getMatFunc(edObj, edObj->mat);
		copyMat4(edObj->mat, edObj->oldMat);
	}
}

void edObjRefreshAllMatrices(void)
{
	int i;
	EditorObject **selection = NULL;
	edObjSelectionGetAll(&selection);
	for (i = 0; i < eaSize(&selection); i++)
		edObjRefreshMat(selection[i]);
	eaDestroy(&selection);
}

/******
* This function refreshes the matrix of a particular EditorObjectType's entire selection; if the
* specified type is EDTYPE_NONE, then ALL types' selection's matrices are refreshed from the type's
* getMatFunc callback.
* PARAMS:
*   objType - EditorObjectTypeEnum of the type whose selection is to be refreshed
******/
void edObjSelectionRefreshMat(EditorObjectTypeEnum objType)
{
	EditorObjectType *type = editorObjectTypeGet(objType);
	int i;

	if (type)
	{
		for (i = 0; i < eaSize(&type->selection); i++)
			edObjRefreshMat(type->selection[i]);
	}
	else
	{
		// refresh matrices on entire selection
		StashTableIterator iter;
		StashElement el;

		stashGetIterator(edObjAllTypes, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			type = stashElementGetPointer(el);
			assert(type);
			for (i = 0; i < eaSize(&type->selection); i++)
				edObjRefreshMat(type->selection[i]);
		}
	}
}

/******
* This function sets the harness's matrix, which is used for the gizmos.
* PARAMS:
*   mat - Mat4 matrix to use for harness gizmos
******/
void edObjHarnessSetGizmoMatrix(const Mat4 mat)
{
	copyMat4(mat, edObjGlobalHarness.mat);
	TranslateGizmoSetMatrix(edObjGlobalHarness.transGizmo, mat);
	RotateGizmoSetMatrix(edObjGlobalHarness.rotGizmo, mat);
	edObjUpdateVolumeGizmoState();
}

/******
* This function does the same thing as edObjHarnessSetGizmoMatrix, except that it actually
* applies the new matrix to the selection and calls the selection's start/end move callbacks.
* PARAMS:
*   mat - Mat4 matrix to use for harness gizmos
******/
void edObjHarnessSetGizmoMatrixAndCallback(const Mat4 mat)
{
	EditorObject **selection = NULL;
	int i;
	bool movementEnabled = true;

	edObjSelectionGetAll(&selection);
	for (i = 0; i < eaSize(&selection); i++)
	{
		if (!selection[i]->type->movementEnableFunc ||
            !selection[i]->type->movementEnableFunc(selection[i], edObjHarnessGetGizmo()))
		{
			movementEnabled = false;
			break;
		}
	}
	eaDestroy(&selection);

	if (movementEnabled)
	{
		edObjHarnessActivate(edObjGlobalHarness.mat, NULL);
		edObjSelectionMoveToMat(mat);
		edObjHarnessSetGizmoMatrix(mat);
		edObjHarnessDeactivate(edObjGlobalHarness.mat, NULL);
	}
}

/******
* This function gets the internal matrix of the harness - the matrix used for the various gizmos.
* PARAMS:
*   outMat - Mat4 where the matrix will be written
******/
void edObjHarnessGetGizmoMatrix(Mat4 outMat)
{
	copyMat4(edObjGlobalHarness.mat, outMat);
}

/******
* This function sets the EditorObject harness gizmos' active matrix according to the current
* selection.
******/
void edObjHarnessGizmoMatrixUpdate(void)
{
	StashTableIterator iter;
	StashElement el;
	EditorObjectType *type;
	Mat4 workingMat;
	int i, count = 0;

	// calculate the "average" of the selection's matrices
	copyMat4(unitmat, workingMat);
	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		type = stashElementGetPointer(el);
		assert(type);
        if (type->getMatFunc)
        {
            for (i = 0; i < eaSize(&type->selection); i++)
            {
				edObjRefreshMat(type->selection[i]);
                if (count == 0)
                    copyMat4(type->selection[i]->mat, workingMat);
                else
                    addVec3(type->selection[i]->mat[3], workingMat[3], workingMat[3]);
                count++;
            }
        }
	}		
	if (count > 1)
	{
		copyMat3(unitmat, workingMat);
		scaleVec3(workingMat[3], 1.0 / count, workingMat[3]);
		
		if(TranslateGizmoGetSpecSnap(edObjGlobalHarness.transGizmo) == EditSnapGrid)
		{
			float snap_res = GizmoGetSnapWidth(TranslateGizmoGetSnapResolution(edObjGlobalHarness.transGizmo));
			#define GIZMO_CLAMP_TO_SNAP(x) (x) = snap_res*(int)(((x)/snap_res)+0.5f)
			GIZMO_CLAMP_TO_SNAP(workingMat[3][0]);
			GIZMO_CLAMP_TO_SNAP(workingMat[3][1]);
			GIZMO_CLAMP_TO_SNAP(workingMat[3][2]);
		}
	}

	edObjHarnessSetGizmoMatrix(workingMat);
}

/******
* This fuction determines whether the harness is in "edit pivot" mode or not.
* PARAMS:
*   mode - EditorObjectPivotMode to set the harness to
******/
void edObjHarnessSetPivotMode(EditorObjectPivotMode mode)
{
	edObjGlobalHarness.pivotMode = mode;
}

/******
* This function enables a particular gizmo on the global selection harness, allowing the user
* to manipulate (if mode is set to a translate or rotate gizmo) the current selection.
* PARAMS:
*   mode - EditorObjectGizmoMode to apply to the harness
******/
void edObjHarnessSetGizmo(EditorObjectGizmoMode mode)
{
	edObjGlobalHarness.gizmoMode = mode;
}

/******
* This function returns the harness's selected gizmo.
******/
EditorObjectGizmoMode edObjHarnessGetGizmo()
{
	return edObjGlobalHarness.gizmoMode;
}

/******
* This function returns the harness's translate gizmo.
* RETURNS:
*   TranslateGizmo used for the harness
******/
TranslateGizmo *edObjHarnessGetTransGizmo(void)
{
	assert(edObjGlobalHarness.transGizmo);
	return edObjGlobalHarness.transGizmo;
}

/******
* This function returns the harness's rotate gizmo.
* RETURNS:
*   RotateGizmo used for the harness
******/
RotateGizmo *edObjHarnessGetRotGizmo(void)
{
	assert(edObjGlobalHarness.rotGizmo);
	return edObjGlobalHarness.rotGizmo;
}

/******
* This function indicates whether one of the internal harness gizmos (translate or rotate) are active.
* RETURNS:
*   bool indicating whether a gizmo is active or not
******/
bool edObjHarnessGizmoIsActive(void)
{
	return (TranslateGizmoIsActive(edObjGlobalHarness.transGizmo) || RotateGizmoIsActive(edObjGlobalHarness.rotGizmo));
}

/******
* This function gets the crossing mode for the marquee selection tool.
* RETURNS:
*   bool indicating whether marquee selection should allow selected objects to overlap the marquee
******/
bool edObjHarnessGetMarqueeCrossingMode(void)
{
	return edObjGlobalHarness.crossingMode;
}

/******
* This function sets the crossing mode for the marquee selection tool.
* PARAMS:
*   crossingMode - bool specifying whether marquee selection should allow selected objects to overlap the marquee
******/
void edObjHarnessSetMarqueeCrossingMode(bool crossingMode)
{
	edObjGlobalHarness.crossingMode = crossingMode;
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "MarqueeCrossingMode", crossingMode);
}

/******
* This function returns whether normal snapping is enabled.
* RETURNS:
*   bool indicating whether normal snapping is on
******/
bool edObjHarnessGetSnapNormal(void)
{
	return TranslateGizmoGetSnapNormal(edObjGlobalHarness.transGizmo);
}

/******
* This function returns whether the translate gizmo is snapping to the inverse of the axis.
* RETURNS:
*   bool indicating whether inverse normal snapping is enabled
******/
bool edObjHarnessGetSnapNormalInverse(void)
{
	return TranslateGizmoGetSnapNormalInverse(edObjGlobalHarness.transGizmo);
}

/******
* This function returns the axis being snapped to by the translate gizmo.
* RETURNS:
*   int of the axis being snapped
******/
int edObjHarnessGetSnapNormalAxis(void)
{
	return TranslateGizmoGetSnapNormalAxis(edObjGlobalHarness.transGizmo);
}


/******
* EDITOR OBJECT SELECTION HARNESS - MAIN
******/
/******
* This is the gizmo activation callback, used on both the translate and rotate gizmos.  It goes through
* each of the EditorObjectTypes registered and calls their startMoveFunc callbacks on their associated
* selection.
* PARAMS:
*   mat - Mat4 of the gizmo when it was activated
******/
static void edObjHarnessActivate(const Mat4 mat, void *unused)
{
	EditorObjectType *type;
	StashTableIterator iter;
	StashElement el;
	int i;

	// call start move function on all types that have a selection
	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		type = stashElementGetPointer(el);
		assert(type);

		// set the selection's matrices
		for (i = 0; i < eaSize(&type->selection); i++)
		{
			if (type->getMatFunc)
				type->getMatFunc(type->selection[i], type->selection[i]->mat);
			copyMat4(type->selection[i]->mat, type->selection[i]->oldMat);
		}

		// call start move function on the type
		if (type->startMoveFunc && eaSize(&type->selection) > 0)
			type->startMoveFunc(type->selection);
	}
	copyMat4(edObjGlobalHarness.mat, edObjGlobalHarness.oldMat);
}

static void edObjHarnessDeactivateMain(const Mat4 mat)
{
	EditorObjectType *type;
	StashTableIterator iter;
	StashElement el;
	Mat4 matCopy;

	copyMat4(mat, matCopy);
	EditUndoBeginGroup(edObjGlobalHarness.edObjUndoStack);

	// call end move function on all types that have a selection
	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		type = stashElementGetPointer(el);
		assert(type);
		if (type->endMoveFunc && eaSize(&type->selection) > 0)
			type->endMoveFunc(type->selection);
	}

	emStatusPrintf("Moved selection.");
	EditUndoEndGroup(edObjGlobalHarness.edObjUndoStack);

	// update the gizmo matrices
	edObjHarnessSetGizmoMatrix(matCopy);
}

/******
* This is the gizmo deactivation callback, used on both the translate and rotate gizmos.  It goes through
* each of the EditorObjectTypes registered and calls their endMoveFunc callbacks on their associated
* selection.
* PARAMS:
*   mat - Mat4 of the gizmo when it was deactivated
******/
void edObjHarnessDeactivate(const Mat4 mat, void *unused)
{
	emQueueFunctionCall(edObjHarnessDeactivateMain, (void*) mat);
}

/******
* This function is used as the translate gizmo's terrain snapping callback.  It gets a terrain
* point of intersection given a cast ray.
* PARAMS:
*   start - Vec3 start of the cast ray
*   end - Vec3 end of the cast ray
*   intersection - Vec3 output where the cast ray intersects the terrain
*   normal - Vec3 output normal to the terrain at the intersection
* RETURNS:
*   bool indicating whether the cast ray hit terrain or not
******/
// TODO: make this static
bool edObjHarnessGetTerrainVert(Vec3 start, Vec3 end, Vec3 intersection, Vec3 normal)
{
	WorldCollCollideResults results;
	int iPartitionIdx = PARTITION_CLIENT;

	U32 filterBits = WC_FILTER_BIT_EDITOR | WC_FILTER_BIT_TERRAIN;
	if (!editorUIState->disableVolColl)
		filterBits |= WC_FILTER_BIT_VOLUME;

	editLibCursorRay(start, end);
	worldCollideRay(iPartitionIdx, start, end, filterBits, &results);

	if(	wcoGetUserPointer(results.wco, heightMapCollObjectMsgHandler, NULL) ||
		worldCollisionEntryIsTerrainFromWCO(results.wco))
	{
		copyVec3(results.posWorldImpact, intersection);
		copyVec3(results.normalWorld, normal);
		return true;
	}
	else
		return false;
}

/******
* This function is used as the translate gizmo's triangle snapping callback.  It returns the triangle
* given a ray to test for intersection with a triangle in the world.
* PARAMS:
*   start - Vec3 start of the cast ray
*   end - Vec3 end of the cast ray
*   outVerts - Mat3 where resulting triangle Vec3's are stored
*   normal - Vec3 where normal to the triangle plane is stored
* RETURNS:
*   bool indicating whether a triangle was found or not
******/
// TODO: make this static
bool edObjHarnessGetModelVert(Vec3 start, Vec3 end, Mat3 outVerts, Vec3 normal)
{
	WorldCollCollideResults results;
	WorldCollisionEntry *resultEntry;
	int iPartitionIdx = PARTITION_CLIENT;

	U32 filterBits = WC_FILTER_BIT_EDITOR | WC_FILTER_BIT_TERRAIN;
	if (!editorUIState->disableVolColl)
		filterBits |= WC_FILTER_BIT_VOLUME;	

	editLibCursorRay(start, end);
	worldCollideRay(iPartitionIdx, start, end, filterBits, &results);

	// get the intersected tracker

	if(	wcoGetUserPointer(results.wco, entryCollObjectMsgHandler, &resultEntry) &&
		resultEntry &&
		resultEntry->model &&
		resultEntry->filter.shapeGroup != WC_SHAPEGROUP_TERRAIN)
	{
		// get the tracker's transformation and apply that to the intersected vertices and return them
		Model *resultModel;
		char colModelName[1024];
		ModelLOD *model_lod;
		const Vec3 *verts;
		const U32 *tris;

		sprintf(colModelName, "%s"MODEL_COLLISION_NAME_SUFFIX, resultEntry->model->name);
		resultModel = modelFind(colModelName, true, resultEntry->model->use_flags);
		if(!resultModel)
			resultModel = resultEntry->model;

		model_lod = modelLoadColLOD(resultModel);

		if (!modelLODIsLoaded(model_lod))
			return false;

		verts = modelGetVerts(model_lod);
		tris = modelGetTris(model_lod);

		if (resultEntry->spline)
		{
			// if the tracker is on a spline, we have to do some extra massaging of vertex data
			int v;
			Vec3 in, out, up, tangent;
			const Vec3 *vec;
			for (v = 0; v < 3; v++)
			{
				vec = &verts[tris[results.tri.index * 3 + v]];
				setVec3(in, (*vec)[0], (*vec)[1], 0);
				splineEvaluate(resultEntry->spline->spline_matrices, 
					-(*vec)[2] / resultEntry->spline->spline_matrices[1][2][0], 
					in, out, up, tangent);
				copyVec3(out, outVerts[v]);
			}
			copyVec3(up, normal);
		}
		else
		{
			// for normal model intersections
			Vec3 tempVec;
			Mat4 scaledMatrix;

			copyMat4(resultEntry->base_entry.bounds.world_matrix, scaledMatrix);
			scaleMat3Vec3(scaledMatrix, resultEntry->scale);

			copyVec3(verts[tris[results.tri.index * 3]], tempVec);
			mulVecMat4(tempVec, scaledMatrix, outVerts[0]);
			copyVec3(verts[tris[results.tri.index * 3 + 1]], tempVec);
			mulVecMat4(tempVec, scaledMatrix, outVerts[1]);
			copyVec3(verts[tris[results.tri.index * 3 + 2]], tempVec);
			mulVecMat4(tempVec, scaledMatrix, outVerts[2]);
			copyVec3(results.normalWorld, normal);
		}

		return true;
	}
	else
		return false;
}

/******
* This function is used for the translate gizmo to allow for null spots in snapping, to which
* nothing snaps; this is mainly used for subselection to prevent the snapping to subselections
* without making them "transparent" to the snapping system.
* PARAMS:
*   start - Vec3 start of the cast ray
*   end - Vec3 end of the cast ray
*   intersection - Vec3 intersection output point
* RETURNS:
*   bool indicating whether an intersection was found
******/
bool edObjHarnessGetNullVert(Vec3 start, Vec3 end, Vec3 intersection)
{
	Vec3 tempIntersection, minIntersection;
	float dist, minDist;

	// search for intersection with patrol points
	minDist = wlePatrolPointCollide(start, end, minIntersection, NULL);

	// search for intersection with encounter actors
	dist = wleEncounterActorCollide(start, end, tempIntersection, NULL);
	if (dist != -1 && (dist < minDist || minDist == -1))
	{
		minDist = dist;
		copyVec3(tempIntersection, minIntersection);
	}

	if (minDist != -1)
	{
		copyVec3(minIntersection, intersection);
		return true;
	}
	else
		return false;
}

void edObjUndoSelectFunction(EditorObject** const selected_list, EditorObject** const deselected_list)
{
	bool selected = false;
	bool deselected = false;
	inside_undo = true;
	if (edObjDeselectListEx(deselected_list, false))
	{
		deselected = true;
	}
	if (edObjSelectList(selected_list, true, true))
	{
		selected = true;
	}
	if ((deselected) && (!selected))	//This check allows the refresh code to run only once if it's needed.
	{
		edObjFinishUp();
	}
	inside_undo = false;
}

static void edObjScaleMinMaxDeactivate(const Mat4 mat, const Vec3 scale[2], void *unused)
{
	if (edObjSelectionGetCount(EDTYPE_NONE) == 1)
	{
		int i;
		EditorObject **selection = edObjSelectionGet(EDTYPE_TRACKER);
		for ( i=0; i < eaSize(&selection); i++ )
		{
			GroupTracker *tracker = trackerFromTrackerHandle(selection[i]->obj);
			if (tracker = wleOpPropsBegin(selection[i]->obj))
			{
				if(tracker->def->property_structs.volume && tracker->def->property_structs.volume->eShape == GVS_Box) {
					copyVec3(scale[0], tracker->def->property_structs.volume->vBoxMin);
					copyVec3(scale[1], tracker->def->property_structs.volume->vBoxMax);
					wleOpPropsUpdate();
				} else if(tracker->def->property_structs.volume && tracker->def->property_structs.volume->eShape == GVS_Sphere) {
					tracker->def->property_structs.volume->fSphereRadius = scale[1][0];
					wleOpPropsUpdate();
				}
				wleOpPropsEnd();
			}
		}
	}	
}

void edObjHarnessInit(void)
{
	// initializing gizmos
	edObjGlobalHarness.transGizmo = TranslateGizmoCreate();
	TranslateGizmoSetActivateCallback(edObjGlobalHarness.transGizmo, edObjHarnessActivate);
	TranslateGizmoSetDeactivateCallback(edObjGlobalHarness.transGizmo, edObjHarnessDeactivate);
	AutoSnapGizmoSetTriGetter(edObjHarnessGetModelVert);
	AutoSnapGizmoSetTerrainF(edObjHarnessGetTerrainVert);
	AutoSnapGizmoSetNullF(edObjHarnessGetNullVert);
	edObjGlobalHarness.rotGizmo = RotateGizmoCreate();
	RotateGizmoSetActivateCallback(edObjGlobalHarness.rotGizmo, edObjHarnessActivate);
	RotateGizmoSetDeactivateCallback(edObjGlobalHarness.rotGizmo, edObjHarnessDeactivate);
	edObjGlobalHarness.scaleMinMaxGizmo = ScaleMinMaxGizmoCreate();
	ScaleMinMaxGizmoSetDeactivateCallback(edObjGlobalHarness.scaleMinMaxGizmo, edObjScaleMinMaxDeactivate);
	edObjGlobalHarness.gizmoMode = EdObjTranslate;
	copyMat4(unitmat, edObjGlobalHarness.mat);
	edObjGlobalHarness.showGizmos = false;
	edObjGlobalHarness.movementEnabled = true;
	edObjGlobalHarness.volumeSelected = false;
	edObjGlobalHarness.marqueeDist = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "MarqueeDistance", HARNESS_MARQ_DEFAULT_DIST);
	edObjGlobalHarness.crossingMode = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "MarqueeCrossingMode", 0);
	edObjGlobalHarness.edObjUndoStack = EditUndoStackCreate();
	EditUndoSetSelectFn(edObjGlobalHarness.edObjUndoStack, edObjUndoSelectFunction);
	EditUndoSetTransformFn(edObjGlobalHarness.edObjUndoStack, edObjUndoTransformFunction);
}

/******
* This function is called once per frame and handles selection inputs as well as calling draw
* callbacks.
******/
void edObjHarnessOncePerFrame(void)
{
	StashTableIterator iter;
	StashElement el;
	int mouseX, mouseY;
	bool clickDetected = false;
	Mat4 newMat;
	int selectMode;
	bool mouseDownEvent;

	// Is movement enabled?
	edObjGlobalHarness.movementEnabled = false;

	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		int i;
		EditorObjectType *type = stashElementGetPointer(el);
		if (eaSize(&type->selection) > 0)
		{
			edObjGlobalHarness.movementEnabled = true;
			if (!type->movementEnableFunc)
			{
				edObjGlobalHarness.movementEnabled = false;
				break;
			}
			for (i = 0; i < eaSize(&type->selection); i++)
				if (!type->movementEnableFunc(type->selection[i], edObjHarnessGetGizmo()))
				{
					edObjGlobalHarness.movementEnabled = false;
					break;
				}
			if (!edObjGlobalHarness.movementEnabled) break;
		}
	}

	// gizmo handling
	if (edObjGlobalHarness.showGizmos && edObjGlobalHarness.movementEnabled && editState.mode != EditPlaceObjects)
	{
		// determine whether in quick place mode or not
		if (inpLevelPeek(INP_SHIFT))
		{
			TranslateGizmoSetRelativeDrag(edObjGlobalHarness.transGizmo, false);
			RotateGizmoSetRelativeDrag(edObjGlobalHarness.rotGizmo, false);
		}
		else
		{
			TranslateGizmoSetRelativeDrag(edObjGlobalHarness.transGizmo, true);
			RotateGizmoSetRelativeDrag(edObjGlobalHarness.rotGizmo, true);
		}

		if(edObjGlobalHarness.volumeSelected && edObjGlobalHarness.gizmoMode == EdObjTranslate) {
			if(TranslateGizmoGetSpecSnap(edObjGlobalHarness.transGizmo) == EditSnapGrid)
				ScaleMinMaxGizmoSetSnapResolution(edObjGlobalHarness.scaleMinMaxGizmo, TranslateGizmoGetSnapResolution(edObjGlobalHarness.transGizmo));
			else 
				ScaleMinMaxGizmoSetSnapResolution(edObjGlobalHarness.scaleMinMaxGizmo, -1);
			ScaleMinMaxGizmoSetMatrix(edObjGlobalHarness.scaleMinMaxGizmo, edObjGlobalHarness.mat);
			ScaleMinMaxGizmoUpdate(edObjGlobalHarness.scaleMinMaxGizmo);
			ScaleMinMaxGizmoDraw(edObjGlobalHarness.scaleMinMaxGizmo);
		}

		// render and update gizmos
		copyMat4(unitmat, newMat);
		if ((!inpLevelPeek(INP_CONTROL) && !inpLevelPeek(INP_ALT)) || edObjHarnessGizmoIsActive())
		{
			if (edObjGlobalHarness.gizmoMode == EdObjTranslate)
			{
				TranslateGizmoUpdate(edObjGlobalHarness.transGizmo);
				TranslateGizmoDraw(edObjGlobalHarness.transGizmo);
			}
			else if (edObjGlobalHarness.gizmoMode == EdObjRotate)
			{
				RotateGizmoUpdate(edObjGlobalHarness.rotGizmo);
				RotateGizmoDraw(edObjGlobalHarness.rotGizmo);
			}
		}
		// apply gizmo changes and call moving functions
		if (edObjGlobalHarness.gizmoMode == EdObjTranslate)
			TranslateGizmoGetMatrix(edObjGlobalHarness.transGizmo, newMat);
		else if (edObjGlobalHarness.gizmoMode == EdObjRotate)
			RotateGizmoGetMatrix(edObjGlobalHarness.rotGizmo, newMat);
		if (edObjGlobalHarness.pivotMode == EdObjEditActual && edObjHarnessGizmoIsActive())
			edObjSelectionMoveToMat(newMat);
		else
			copyMat4(newMat, edObjGlobalHarness.mat);
	}

	stashGetIterator(edObjAllTypes, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		EditorObjectType *type = stashElementGetPointer(el);
		if (type->drawFunc)
			type->drawFunc(type->selection);
	}

	if (edObjGlobalHarness.selectionDisabled)
		return;

	// selection mode
	if (inpLevelPeek(INP_CONTROL) && !inpLevelPeek(INP_ALT))
		selectMode = 0;
	else if (inpLevelPeek(INP_ALT) && !inpLevelPeek(INP_CONTROL))
		selectMode = 1;
	else
		selectMode = 2;

	mousePos(&mouseX, &mouseY);
	mouseDownEvent = mouseDown(MS_LEFT);
	if (mouseDownEvent && !edObjGlobalHarness.quickPlaceTimer)
	{
		edObjGlobalHarness.quickPlaceTimer = timerAlloc();
		edObjGlobalHarness.marqueeStartX = mouseX;
		edObjGlobalHarness.marqueeStartY = mouseY;
		if (editState.mode == EditNormal)
			edObjGlobalHarness.marqueeDrag = true;
		timerStart(edObjGlobalHarness.quickPlaceTimer);
		inpHandled();
	}

	// end timer if the mouse button is released or if the mouse moves
	if (edObjGlobalHarness.quickPlaceTimer &&
		(!mouseIsDown(MS_LEFT) || mouseX != edObjGlobalHarness.marqueeStartX || mouseY != edObjGlobalHarness.marqueeStartY))
	{
		timerFree(edObjGlobalHarness.quickPlaceTimer);
		edObjGlobalHarness.quickPlaceTimer = 0;
	}

	// click selection (act like a normal click selection if quick place timer reaches
	// threshold)
	if (mouseClick(MS_LEFT) || (edObjGlobalHarness.quickPlaceTimer && timerElapsed(edObjGlobalHarness.quickPlaceTimer) >= HARNESS_QUICKPLACE_TIME))
	{
		int i;
		float dist = -1;
		EditorObject **closestSelection = NULL;

		stashGetIterator(edObjAllTypes, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			EditorObjectType *type = stashElementGetPointer(el);
			EditorObject **selectedObjs = NULL;
			float currDist;

			if (!type->clickFunc)
				continue;

			// poll each type for whether something of the type is underneath the mouse
			currDist = type->clickFunc(mouseX, mouseY, &selectedObjs);
			if (eaSize(&selectedObjs) == 0)
				continue;

			// ref each object
			for (i = 0; i < eaSize(&selectedObjs); i++)
				editorObjectRef(selectedObjs[i]);

			// determine the closest of these types (to the camera)
			if (dist == -1 || currDist < dist)
			{
				dist = currDist;

				// deref the last cached editor objects, since they are no longer the closest
				// and will not be selected
				eaDestroyEx(&closestSelection, editorObjectDeref);
				eaCopy(&closestSelection, &selectedObjs);
				eaDestroy(&selectedObjs);
			}
			else
				// delete the returned objects, as they are not closest selection
				eaDestroyEx(&selectedObjs, editorObjectDeref);
		}

		if (eaSize(&closestSelection) > 0)
		{
			WleFilter *filter = ui_ComboBoxGetSelectedObject(editorUIState->toolbarUI.marqueeFilterCombo);
			EditorObject **eaSelectionList = NULL, **eaDerefList = NULL;

			// only select things open on the tracker tree unless bore clicking
			EditUndoBeginGroup(edObjGlobalHarness.edObjUndoStack);
			for (i = 0; i < eaSize(&closestSelection); i++)
			{
				EditorObject *actualSelection = closestSelection[i];
				EditorObject *lastFilterMatch = NULL;

				editorObjectRef(actualSelection);

				if (!editState.inputData.boreKey &&
					(!wleUITrackerTreeGetNodeForEdObj(closestSelection[i]) || (filter && filter->ignoreNodeState)))
				{
					// loop to top of tree to get topmost object matching the filter
					// if the filter is ignoring node state
					do 
					{
						EditorObject *oldSelection = actualSelection;

						ANALYSIS_ASSUME(actualSelection != NULL); // not sure about this!
						if (filter && filter->ignoreNodeState && wleFilterApply(actualSelection, filter))
						{
							if (lastFilterMatch)
								editorObjectDeref(lastFilterMatch);
							lastFilterMatch = actualSelection;
							editorObjectRef(lastFilterMatch);
						}

						if (oldSelection->type->parentFunc)
							actualSelection = oldSelection->type->parentFunc(oldSelection);
						else
							actualSelection = NULL;

						if (oldSelection)
							editorObjectDeref(oldSelection);
						if (actualSelection)
							editorObjectRef(actualSelection);

					} while (actualSelection && ((filter && filter->ignoreNodeState) || !wleUITrackerTreeGetNodeForEdObj(actualSelection)));
				}

				if (!editState.inputData.boreKey && filter && filter->ignoreNodeState)
				{
					if (actualSelection)
					{
						editorObjectDeref(actualSelection);
						actualSelection = NULL;
					}

					if (lastFilterMatch)
						actualSelection = lastFilterMatch;
				}

				if (actualSelection &&
					((!editState.inputData.boreKey && filter && filter->ignoreNodeState) || wleFilterApply(actualSelection, filter)))
				{
					eaPush(&eaSelectionList, actualSelection ? actualSelection : closestSelection[i]);
				}

				if (actualSelection)
				{
					eaPush(&eaDerefList, actualSelection);
				}
			}

			if (selectMode == 0)
			{
				edObjSelectList(eaSelectionList, true, true);
			}
			else if (selectMode == 1)
			{
				edObjDeselectList(eaSelectionList);
			}
			else
			{
				edObjSelectList(eaSelectionList, false, true);	
			}
			eaDestroy(&eaSelectionList);

			for (i = 0; i < eaSize(&closestSelection); i++)
			{
				if (!i)
				{
					if (edObjGlobalHarness.lastClicked)
						editorObjectDeref(edObjGlobalHarness.lastClicked);
					edObjGlobalHarness.lastClicked = closestSelection[i];
					editorObjectRef(edObjGlobalHarness.lastClicked);
				}

			}
			
			for (i = 0; i < eaSize(&eaDerefList); i++)
			{
				editorObjectDeref(eaDerefList[i]);
			}
			eaDestroy(&eaDerefList);

			EditUndoEndGroup(edObjGlobalHarness.edObjUndoStack);
			eaDestroyEx(&closestSelection, editorObjectDeref);
			inpHandled();
		}
		else if (selectMode == 2)
			edObjSelectionClear(EDTYPE_NONE);
		clickDetected = true;
	}

	// go into placement mode if the timer has reached its threshold
	if (edObjGlobalHarness.quickPlaceTimer &&
		timerElapsed(edObjGlobalHarness.quickPlaceTimer) >= HARNESS_QUICKPLACE_TIME &&
		edObjSelectionGetCount(EDTYPE_NONE) > 0)
	{
		wleEdObjSelectionPlace();
		edObjHarnessActivate(NULL, NULL);
		timerFree(edObjGlobalHarness.quickPlaceTimer);
		edObjGlobalHarness.quickPlaceTimer = 0;
		edObjGlobalHarness.marqueeDrag = edObjGlobalHarness.marqueeDragDist = false;
	}

	// marquee selection
	if (!mouseIsDown(MS_LEFT) && edObjGlobalHarness.marqueeDrag)
	{
		EditorObject **marqueeSelection = NULL;
		int i;

		// end marquee drag
		edObjGlobalHarness.marqueeDrag = edObjGlobalHarness.marqueeDragDist = false;
		mouseLock(false);

		// ensure that the mouseUp detected is not because of a single-click - don't want to call
		// marquee detection functions on every click as they can be expensive
		if (clickDetected)
			return;

		stashGetIterator(edObjAllTypes, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			EditorObjectType *type = stashElementGetPointer(el);
			Mat44 viewMat, scrProjMat;
			GfxCameraView* view = gfxGetActiveCameraView();
			int temp;

			if (!type->marqueeFunc)
				continue;

			// ensure marquee X and Y parameters to click function are top-left and bottom-right of rectangle
			if (edObjGlobalHarness.marqueeStartX > edObjGlobalHarness.marqueeEndX)
			{
				temp = edObjGlobalHarness.marqueeEndX;
				edObjGlobalHarness.marqueeEndX = edObjGlobalHarness.marqueeStartX;
				edObjGlobalHarness.marqueeStartX = temp;
			}
			if (edObjGlobalHarness.marqueeStartY > edObjGlobalHarness.marqueeEndY)
			{
				temp = edObjGlobalHarness.marqueeEndY;
				edObjGlobalHarness.marqueeEndY = edObjGlobalHarness.marqueeStartY;
				edObjGlobalHarness.marqueeStartY = temp;
			}

			// poll each type for whether something of the type is in the marquee
			mat43to44(view->frustum.viewmat, viewMat);
			mulMat44Inline(view->projection_matrix, viewMat, scrProjMat);
			type->marqueeFunc(edObjGlobalHarness.marqueeStartX, edObjGlobalHarness.marqueeStartY, edObjGlobalHarness.marqueeEndX, edObjGlobalHarness.marqueeEndY, edObjGlobalHarness.marqueeDist, view->frustum.cammat, scrProjMat, &marqueeSelection, edObjGlobalHarness.crossingMode);
		}

		// select the objects in the marquee (if some were found)
		if (eaSize(&marqueeSelection) > 0)
		{
			// first deselect everything
			EditUndoBeginGroup(edObjGlobalHarness.edObjUndoStack);

			// ref everything
			for (i = 0; i < eaSize(&marqueeSelection); i++)
				editorObjectRef(marqueeSelection[i]);

			// select as appropriate
			if (selectMode == 0)
			{
				edObjSelectList(marqueeSelection, true, true);
			}
			else if (selectMode == 1)
			{
				edObjDeselectList(marqueeSelection);
			}
			else
			{
				edObjSelectList(marqueeSelection, false, true);
			}

			EditUndoEndGroup(edObjGlobalHarness.edObjUndoStack);
			if (eaSize(&marqueeSelection) > 1)
			{
				wleAESetActiveQueued(editorUIState->trackerTreeUI.allSelect);
				wleUITrackerTreeHighlightEdObj(NULL);
			}

			// deref everything
			eaDestroyEx(&marqueeSelection, editorObjectDeref);
		}
		else if (selectMode == 2)
			edObjSelectionClear(EDTYPE_NONE);
	}
	if (edObjGlobalHarness.marqueeDrag)
	{
		Mat4 cam, camInv;
		Vec3 min, max, temp;
		Vec3 start, v1, v2;
		float distAdj;

		// user can change marquee distance by holding SHIFT and moving mouse up or down
		if (inpEdgePeek(INP_SHIFT))
		{
			edObjGlobalHarness.marqueeDragDist = true;
			edObjGlobalHarness.marqueeEndX = mouseX;
			edObjGlobalHarness.marqueeEndY = mouseY;
			mouseLock(true);
			inpHandled();
		}
		else if (!inpLevelPeek(INP_SHIFT) && edObjGlobalHarness.marqueeDragDist)
		{
			edObjGlobalHarness.marqueeDragDist = false;
			EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "MarqueeDistance", edObjGlobalHarness.marqueeDist);
			mouseLock(false);
		}
		if (edObjGlobalHarness.marqueeDragDist)
		{
			int diffX, diffY;
			float distDiff;
			mouseDiffLegacy(&diffX, &diffY);
			distDiff = diffY * HARNESS_MARQ_MOUSE_DIFF_SCALE;
			edObjGlobalHarness.marqueeDist -= distDiff;
			if (edObjGlobalHarness.marqueeDist < HARNESS_MARQ_DIST_MIN)
				edObjGlobalHarness.marqueeDist = HARNESS_MARQ_DIST_MIN;
			if (edObjGlobalHarness.marqueeDist > HARNESS_MARQ_DIST_MAX)
				edObjGlobalHarness.marqueeDist = HARNESS_MARQ_DIST_MAX;
			inpHandled();
		}
		else
		{
			edObjGlobalHarness.marqueeEndX = mouseX;
			edObjGlobalHarness.marqueeEndY = mouseY;
		}

		// draw the marquee outline
		gfxDrawLineWidth(edObjGlobalHarness.marqueeStartX, edObjGlobalHarness.marqueeStartY, 10000, edObjGlobalHarness.marqueeEndX, edObjGlobalHarness.marqueeStartY, ColorRed, 1);
		gfxDrawLineWidth(edObjGlobalHarness.marqueeStartX, edObjGlobalHarness.marqueeStartY, 10000, edObjGlobalHarness.marqueeStartX, edObjGlobalHarness.marqueeEndY, ColorRed, 1);
		gfxDrawLineWidth(edObjGlobalHarness.marqueeEndX, edObjGlobalHarness.marqueeStartY, 10000, edObjGlobalHarness.marqueeEndX, edObjGlobalHarness.marqueeEndY, ColorRed, 1);
		gfxDrawLineWidth(edObjGlobalHarness.marqueeStartX, edObjGlobalHarness.marqueeEndY, 10000, edObjGlobalHarness.marqueeEndX, edObjGlobalHarness.marqueeEndY, ColorRed, 1);

		// draw the far plane as a very thin box so that it is clear which objects are behind the far
		// plane and will not be selected
		// TODO: use inverse projection matrix?
		gfxGetActiveCameraMatrix(cam);
		scaleVec3(cam[2], -1, v2);
		editLibCursorRayEx(cam, edObjGlobalHarness.marqueeStartX, edObjGlobalHarness.marqueeStartY, start, v1);
		distAdj = edObjGlobalHarness.marqueeDist / dotVec3(v1, v2);
		editLibCursorSegment(cam, edObjGlobalHarness.marqueeStartX, edObjGlobalHarness.marqueeStartY, distAdj, temp, min);
		editLibCursorRayEx(cam, edObjGlobalHarness.marqueeEndX, edObjGlobalHarness.marqueeEndY, start, v1);
		distAdj = edObjGlobalHarness.marqueeDist / dotVec3(v1, v2);
		editLibCursorSegment(cam, edObjGlobalHarness.marqueeEndX, edObjGlobalHarness.marqueeEndY, distAdj, temp, max);
		invertMat4Copy(cam, camInv);
		mulVecMat4(min, camInv, temp);
		copyVec3(temp, min);
		mulVecMat4(max, camInv, temp);
		copyVec3(temp, max);
		max[2] -= (edObjGlobalHarness.marqueeDist / 10000);
		gfxDrawBox3D(min, max, cam, colorFromRGBA(0xFFFFFFAA), 0);
	}
}

void edObjHarnessEnableSelection(bool enabled)
{
	edObjGlobalHarness.selectionDisabled = !enabled;
}

EditUndoStack *edObjGetUndoStack(void)
{
	assert(edObjGlobalHarness.edObjUndoStack);
	return edObjGlobalHarness.edObjUndoStack;
}

/********************
* UTIL
********************/
bool edObjIsDescendant(EditorObject *parent, EditorObject *child)
{
	bool ret;
	editorObjectRef(child);
	while (child)
	{
		ANALYSIS_ASSUME(child != NULL);
		if (edObjCompare(parent, child) != 0)
		{
			EditorObject *oldChild = child;
			child = child->type->parentFunc ? child->type->parentFunc(oldChild) : NULL;
			editorObjectDeref(oldChild);
			if (child)
			{
				ANALYSIS_ASSUME(child != NULL);
				editorObjectRef(child);
			}
		}
		else
		{
			break;
		}
	}
	ret = !!child;
	if (child)
		editorObjectDeref(child);
	return ret;
}

#endif

#include "EditorObject_h_ast.c"