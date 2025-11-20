#define GENESIS_ALLOW_OLD_HEADERS
#include "WorldEditorClientMain.h"

#include "WorldEditor.h"
#include "WorldEditorUI.h"
#include "WorldEditorMenus.h"
#include "WorldEditorPlacementAttributes.h"
#include "WorldEditorOperations.h"
#include "WorldEditorOptions.h"
#include "WorldEditorGizmos.h"
#include "WorldEditorUtil.h"
#include "WorldEditorNotes.h"
#include "WorldEditorPrivate.h"
#include "wlVolumes.h"
#include "wlState.h"
#include "CurveEditor.h"
#include "EditorObjectMenus.h"
#include "EditorPrefs.h"
#include "ExpressionEditor.h"
#include "crypt.h"
#include "WorldColl.h"
#include "WorldLib.h"
#include "GfxPrimitive.h"
#include "GfxTexturesPublic.h"
#include "GfxDumpToVrml.h"
#include "inputLib.h"
#include "GfxSpriteText.h"
#include "cmdparse.h"
#include "ObjectLibrary.h"
#include "wlEncounter.h"
#include "RoomConn.h"
#include "wlEditorIncludes.h"
#include "ProgressOverlay.h"
#include "wlModel.h"
#include "WorldGrid.h"
#include "Materials.h"
#include "wlGroupPropertyStructs.h"
#include "wlGenesis.h"
#include "encounter_common.h"
#include "GenesisMapDescriptionEditor.h"
#include "dynWind.h"
#include "sysutil.h"
#include "timing.h"
#include "partition_enums.h"
#include "UGCCommon.h"
#include "bounds.h"
#include "WorldEditorExporter.h"
#include "StringCache.h"

#include "WorldEditorClientMain_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#ifndef NO_EDITORS

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/******
* CONSTANTS AND MACROS
******/
#define MAX_AUTOSAVES 10
#define OBJ_PLACE_DIST 50

const Vec3 wlePatrolArrowMin = {-1.5f,-0.5f,-0.25f};
const Vec3 wlePatrolArrowMax = {1.5f,0.5f,2.75f};
const Vec3 wlePatrolEndMin = {-1.5f,-0.5f,-1.5f};
const Vec3 wlePatrolEndMax = {1.5f,0.5f,1.5f};

const Vec3 wleActorMin = {-1.0f, -0.25, -0.5};
const Vec3 wleActorMax = {1.0f, 6, 0.5};

/******
* EXTERNS
******/
#undef gfxfont_Printf
void gfxfont_Printf(float x, float y, float z, float xsc, float ysc, int flags, const char *fmt, ...);
#define gfxfont_Printf(x, y, z, xsc, ysc, flags, fmt, ...) gfxfont_Printf(x, y, z, xsc, ysc, flags, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

extern StaticDefineInt EditSpecialSnapModeEnum[]; 

/******
* FORWARD DECLARATIONS
******/
static void wlePatrolPointGetMat(WorldPatrolProperties *patrol, int pointIdx, Mat4 worldMat, Mat4 outMat, bool snapToY, bool validDrawMat, bool movingUseWorldMat);
static void wleEdObjTrackerSelectionChanged(EditorObject **selection, bool in_undo);
static void wleEdObjTrackerEndMove(EditorObject **selection);
static void wlePatrolPathRefreshMat(WorldPatrolProperties *patrol, Mat4 worldMat, bool snapToY, bool skipMoving);
static void wleEncounterActorRefreshMat(WorldActorProperties *actor, Mat4 worldMat, bool snapToY);
static void wleRefreshStateRecurse(GroupTracker *tracker, bool updateLists);
static void wleEncounterActorActionDuplicate(EditorObject **selection);
static void wlePatrolPointActionDuplicate(EditorObject **selection);
void editCmdToggleWidget(void);

/******
* GLOBAL VARIABLES
******/
// state of the editor
EditState editState;

// editor manager's editor struct
EMEditor worldEditor;
EMPicker skyPicker;

// Draw mode selection
int g_DrawAllAggro = 0;
AUTO_CMD_INT(g_DrawAllAggro, DrawAllAggro);

/******
* ACCESSORS
******/
EMEditorDoc *wleGetWorldEditorDoc(void)
{
	return eaSize(&worldEditor.open_docs) > 0 ? worldEditor.open_docs[0] : NULL;
}

/********************
* COPY BUFFER
********************/
WleCopyBuffer ***wleCopyBufferDup(WleCopyBuffer **buffer)
{
	WleCopyBuffer ***ret = calloc(1, sizeof(*ret));
	int i;

	for (i = 0; i < eaSize(&buffer); i++)
	{
		WleCopyBuffer *copiedBuffer = calloc(1, sizeof(*copiedBuffer));
		copiedBuffer->pti = buffer[i]->pti;
		copiedBuffer->data = StructCloneVoid(copiedBuffer->pti, buffer[i]->data);
		copiedBuffer->useCurrentSelection = buffer[i]->useCurrentSelection;
		copyMat4(buffer[i]->relMat, copiedBuffer->relMat);
		eaPush(ret, copiedBuffer);
	}

	return ret;
}

static void wleCopyBufferClear(WleCopyBuffer **buffer)
{
	int i;
	for (i = 0; i < eaSize(&buffer); i++)
	{
		if (buffer[i]->pti && buffer[i]->data)
			StructDestroyVoid(buffer[i]->pti, buffer[i]->data);
		free(buffer[i]);
	}
	eaDestroy(&buffer);
}

static void wleCopyBufferFree(WleCopyBuffer ***buffer)
{
	wleCopyBufferClear(*buffer);
	free(buffer);
}

static void wleCopyBufferAdd(WleCopyBuffer ***buffer, void *data, ParseTable *pti, Mat4 relMat)
{
	WleCopyBuffer *newBuffer = calloc(1, sizeof(*newBuffer));
	newBuffer->data = data;
	newBuffer->pti = pti;
	copyMat4(relMat, newBuffer->relMat);
	eaPush(buffer, newBuffer);
}

/******
* ENCOUNTER SUBOBJECT HANDLES
******/
/******
* This function returns the patrol properties from the encounter object associated with
* the specified subhandle.
* PARAMS:
*   handle - WleEncObjSubHandle whose patrol properties are to be returned
* RETURNS:
*   WorldPatrolProperties belonging to the encounter object pointed to by handle
******/
static WorldPatrolProperties *wlePatrolFromHandle(const WleEncObjSubHandle *handle, Mat4 patrolMat)
{
	GroupTracker *tracker;
	GroupDef *def;

	if (!handle)
		return NULL;
	tracker = trackerFromTrackerHandle(handle->parentHandle);
	if(!tracker)
		return NULL;
	def = tracker->def;
	if(!def)
		return NULL;
	if(patrolMat)
		trackerGetMat(tracker, patrolMat);
	return def->property_structs.patrol_properties;
}

/******
* This function returns the patrol point properties from the encounter object associated
* with the specified subhandle.
* PARAMS:
*   handle - WleEncObjSubHandle whose patrol point properties are to be returned
* RETURNS:
*   WorldPatrolPointProperties belonging to the encounter object pointed to by handle
******/
WorldPatrolPointProperties *wlePatrolPointFromHandle(const WleEncObjSubHandle *handle, Mat4 pointMat)
{
	Mat4 patrolMat;
	WorldPatrolProperties *patrolProperties = wlePatrolFromHandle(handle, pointMat ? patrolMat : NULL);

	if (patrolProperties && handle->childIdx >= 0 && handle->childIdx < eaSize(&patrolProperties->patrol_points))
	{
		WorldPatrolPointProperties *pointProperties = patrolProperties->patrol_points[handle->childIdx];
		if(pointMat)
			wlePatrolPointGetMat(patrolProperties, handle->childIdx, patrolMat, pointMat, false, false, true);
		return pointProperties;
	}
	return NULL;
}

/******
* This function returns the actual encounter properties from the encounter object associated with
* the specified subhandle.  Note that this does NOT return the properties on the GroupDef.
* PARAMS:
*   handle - WleEncObjSubHandle whose encounter properties are to be returned
* RETURNS:
*   WorldEncounterProperties belonging to the encounter object pointed to by handle
******/
WorldEncounterProperties *wleEncounterFromHandle(const WleEncObjSubHandle *handle)
{
	GroupTracker *tracker;

	if (!handle)
		return NULL;

	tracker = trackerFromTrackerHandle(handle->parentHandle);
	if (!tracker || !tracker->enc_obj || tracker->enc_obj->type != WL_ENC_ENCOUNTER)
		return NULL;
	return ((WorldEncounter*) tracker->enc_obj)->properties;
}

/******
* This function returns the encounter actor properties from the object associated
* with the specified subhandle.
* PARAMS:
*   handle - WleEncObjSubHandle whose encounter actor properties are to be returned
* RETURNS:
*   WorldActorProperties belonging to the encounter object pointed to by handle
*	actorMat - matrix of actor, if a matrix is passed in 
******/
WorldActorProperties *wleEncounterActorFromHandle(const WleEncObjSubHandle *handle, Mat4 actorMat)
{
	WorldEncounterProperties *encounterProperties;
	GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);
	GroupDef *def;
	if(!tracker)
		return NULL;
	def = tracker->def;
	if(!def)
		return NULL;
	encounterProperties = def->property_structs.encounter_properties;
	if (encounterProperties && handle->childIdx >= 0 && handle->childIdx < eaSize(&encounterProperties->eaActors))
	{
		WorldActorProperties *actor = encounterProperties->eaActors[handle->childIdx];
		if(actorMat)
		{
			Mat4 worldMat;
			Mat4 tempMat;
			trackerGetMat(tracker, worldMat);
			createMat3YPR(tempMat, actor->vRot);
			copyVec3(actor->vPos, tempMat[3]);
			mulMat4(worldMat, tempMat, actorMat);
		}
		return actor;
	}
	return NULL;
}

/******
* This function returns an encounter object subhandle to reference patrol points.
* PARAMS:
*   trackerHandle - TrackerHandle to the tracker associated with the patrol path 
*   childIdx - int index of the patrol point or encounter actor within with tracker
* RETURNS:
*   WleEncObjSubHandle that points to the specified child
******/
WleEncObjSubHandle *wleEncObjSubHandleCreate(const TrackerHandle *trackerHandle, int childIdx)
{
	GroupTracker *tracker = trackerFromTrackerHandle(trackerHandle);
	WleEncObjSubHandle *handle;

	if (!tracker || !tracker->def ||
		(!tracker->def->property_structs.patrol_properties && !tracker->def->property_structs.encounter_properties))
		return NULL;

	handle = StructCreate(parse_WleEncObjSubHandle);
	handle->parentHandle = trackerHandleCopy(trackerHandle);
	handle->childIdx = childIdx;
	return handle;
}

/******
* This function destroys the specified subhandle.
* PARAMS:
*   handle - WleEncObjSubHandle to destroy
******/
static void wleEncObjSubHandleDestroy(WleEncObjSubHandle *handle)
{
	if (!handle)
		return;

	StructDestroy(parse_WleEncObjSubHandle, handle);
}

/******
* TRACKER STATE MANAGEMENT
******/
static void wleChangeMode(EditMode mode)
{
	EMEditorDoc *doc = wleGetWorldEditorDoc();
	EditMode oldMode = editState.mode;
	editState.mode = mode;
	assert(doc);
	if (doc && mode != EditNormal)
		doc->edit_undo_stack = NULL;
	else
		doc->edit_undo_stack = edObjGetUndoStack();

	if (mode == EditPlaceObjects)
	{
		// set initial pivot matrix
		copyMat4(unitmat, editState.quickPlaceState.pivotMat);

		// disable pivoting and pivot editing
		wleSetGizmoMode(EditActual);
		wleUIGizmoModeEnable(false);
	}
	else
	{
		if (editState.quickPlaceState.buffer)
			wleCopyBufferFree(editState.quickPlaceState.buffer);
		editState.quickPlaceState.buffer = NULL;

		// refresh subobject matrices
		if (oldMode == EditPlaceObjects)
		{
			EditorObject **subHandles;
			TrackerHandle **selection = NULL;
			GroupTracker **trackersToRefresh = NULL;
			int i;

			wleSelectionGetTrackerHandles(&selection);
			for (i = 0; i < eaSize(&selection); i++)
			{
				GroupTracker *tracker = trackerFromTrackerHandle(selection[i]);
				trackerSetInvisible(tracker, false);
				if (tracker)
					eaPushUnique(&trackersToRefresh, tracker);
			}
			subHandles = edObjSelectionGet(EDTYPE_ENCOUNTER_ACTOR);
			for (i = 0; i < eaSize(&subHandles); i++)
			{
				WleEncObjSubHandle *subHandle = subHandles[i]->obj;
				WorldActorProperties *actor = subHandle ? wleEncounterActorFromHandle(subHandle, NULL) : NULL;
				GroupTracker *tracker = subHandle ? trackerFromTrackerHandle(subHandle->parentHandle) : NULL;
				if (actor)
					actor->moving = 0;
				if (tracker)
					eaPushUnique(&trackersToRefresh, tracker);
			}
			subHandles = edObjSelectionGet(EDTYPE_PATROL_POINT);
			for (i = 0; i < eaSize(&subHandles); i++)
			{
				WleEncObjSubHandle *subHandle = subHandles[i]->obj;
				WorldPatrolPointProperties *point = subHandle ? wlePatrolPointFromHandle(subHandle, NULL) : NULL;
				GroupTracker *tracker = subHandle ? trackerFromTrackerHandle(subHandle->parentHandle) : NULL;
				if (point)
					point->moving = 0;
				if (tracker)
					eaPushUnique(&trackersToRefresh, tracker);
			}
			for (i = 0; i < eaSize(&trackersToRefresh); i++)
				wleRefreshStateRecurse(trackersToRefresh[i], false);
			
			editState.drawGhosts = false;
			eaDestroy(&selection);
			eaDestroy(&trackersToRefresh);
		}
		wleUIGizmoModeEnable(true);
	}
}

/******
* This is the main function responsible for causing a tracker to be invisible in the world.  This does
* nothing to manage the EditState's list of hidden trackers.
* PARAMS:
*   tracker - GroupTracker to hide
******/
static void wleTrackerHideMain(GroupTracker *tracker)
{
	int i;
	bool refresh = false;

	if (!tracker->invisible)
	{
		trackerSetInvisible(tracker, true);
		refresh = true;
	}
	if (!tracker->parent)
	{
		// Hiding the scratch layer should update the UI
		if(tracker->parent_layer->scratch && editorUIState && editorUIState->showingScratchLayer) {
			editorUIState->showingScratchLayer = false;
			wleToolbarUpdateEditingScratchLayer(tracker->parent_layer);
		}

		for (i = 0; i < eaSize(&wleMapLayers); i++)
		{
			if (tracker->parent_layer == emMapLayerGetUserPtr(wleMapLayers[i]) && emMapLayerGetVisible(wleMapLayers[i]))
			{
				emMapLayerSetVisible(wleMapLayers[i], 0);
				refresh = true;
			}
		}
	}

	if (refresh)
	{
		wleRefreshState();
		wleUITrackerTreeRefresh(NULL);
		wleAERefresh();
	}
}

/******
* This is the main function for setting a tracker's selected flags and doing other necessary things
* whenever a tracker is selected or a selected tracker is refreshed.
* PARAMS:
*   tracker - GroupTracker to select
******/
static void wleTrackerSelectMain(GroupTracker *tracker)
{
	trackerSetSelected(tracker, true);
	tracker->open = 0;
	while(tracker = tracker->parent)
		tracker->open = 1;
}

static void wleRefreshStateRecurse(GroupTracker *tracker, bool updateLists)
{
	int i;

	if (!tracker)
		return;

	if (tracker->def && tracker->def->property_structs.patrol_properties)
	{
		// refresh draw matrices on patrol points
		if (tracker->enc_obj && tracker->enc_obj->type == WL_ENC_PATROL_ROUTE && !EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePatrolPoints", 0))
			wlePatrolPathRefreshMat(((WorldPatrolRoute*) tracker->enc_obj)->properties, NULL, false, false);

		if (updateLists)
			eaPush(&editState.patrolTrackers, trackerHandleCreate(tracker));
	}
	if (tracker->def && tracker->def->property_structs.encounter_properties)
	{
		// refresh draw matrices on actors
		if (tracker->enc_obj && tracker->enc_obj->type == WL_ENC_ENCOUNTER && !EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideEncounterActors", 0))
		{
			WorldEncounterProperties *encounter = ((WorldEncounter*) tracker->enc_obj)->properties;
			for (i = 0; i < eaSize(&encounter->eaActors); i++)
				wleEncounterActorRefreshMat(encounter->eaActors[i], NULL, false);
		}

		if (updateLists)
			eaPush(&editState.encounterTrackers, trackerHandleCreate(tracker));
	}
	if (tracker->def && tracker->def->property_structs.curve)
	{
		if (updateLists)
			eaPush(&editState.curveTrackers, trackerHandleCreate(tracker));
	}
	for (i = 0; i < tracker->child_count; i++)
		wleRefreshStateRecurse(tracker->children[i], updateLists);
}

/******
* This function scans the current tracker selection and removes any invalid TrackerHandles and resets
* the selection flag on existing trackers; this also scans the other lists maintained by the edit state,
* including hidden and frozen lists.  This should be called after any alteration of the data - this includes
* world editor operations, reload callbacks, and server updates.
******/
void wleRefreshState(void)
{
	EditorObject **patrolPoints;
	EditorObject **encounterActors;
	TrackerHandle **handles = NULL;
	GroupTracker *tracker;
	int i;
	EditorObject **deselectionList = NULL;

	// traverse the tracker tree and compile a list of patrols
	eaDestroyEx(&editState.patrolTrackers, trackerHandleDestroy);
	eaDestroyEx(&editState.encounterTrackers, trackerHandleDestroy);
	eaDestroyEx(&editState.curveTrackers, trackerHandleDestroy);
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		wleRefreshStateRecurse(layerGetTracker(layer), true);
	}

	// refreshing selected trackers
	wleSelectionGetTrackerHandles(&handles);
	for (i = eaSize(&handles) - 1; i >= 0; i--)
	{
		tracker = trackerFromTrackerHandle(handles[i]);
		if (!tracker)
			wleTrackerDeselect(handles[i]);
		else
			wleTrackerSelectMain(tracker);
	}
	eaDestroy(&handles);

	// refreshing selected patrol points
	patrolPoints = edObjSelectionGet(EDTYPE_PATROL_POINT);
	for (i = eaSize(&patrolPoints) - 1; i >= 0; i--)
	{
		WorldPatrolPointProperties *point = wlePatrolPointFromHandle(patrolPoints[i]->obj, NULL);
		if (!point)
			eaPush(&deselectionList, patrolPoints[i]);
		else
			point->selected = 1;
	}

	// refreshing selected encounter actors
	encounterActors = edObjSelectionGet(EDTYPE_ENCOUNTER_ACTOR);
	for (i = eaSize(&encounterActors) - 1; i >= 0; i--)
	{
		WorldActorProperties *actor = wleEncounterActorFromHandle(encounterActors[i]->obj, NULL);
		if (!actor)
			eaPush(&deselectionList, encounterActors[i]);
		else
			actor->selected = 1;
	}

	if (deselectionList)
	{
		edObjDeselectList(deselectionList);
		eaDestroy(&deselectionList);
	}

	// refreshing hidden trackers
	for (i = eaSize(&editState.hiddenTrackers) - 1; i >= 0; i--)
	{
		// As this is a recursive function editState.hiddenTrackers could have been reduced. This check will prevent reading the earray past the end
		if(i >= eaSize(&editState.hiddenTrackers))
		{
			continue;
		}
		tracker = trackerFromTrackerHandle(editState.hiddenTrackers[i]);
		if (!tracker)
			wleTrackerUnhide(editState.hiddenTrackers[i]);
		else
			wleTrackerHideMain(tracker);
	}

	// refreshing frozen trackers
	for (i = eaSize(&editState.frozenTrackers) - 1; i >= 0; i--)
	{
		tracker = trackerFromTrackerHandle(editState.frozenTrackers[i]);
		if (!tracker)
			wleTrackerUnfreeze(editState.frozenTrackers[i]);
		else
			tracker->frozen = 1;
	}

	// refreshing editable trackers
	for (i = eaSize(&editState.editingTrackers) - 1; i >= 0; i--)
	{
		tracker = trackerFromTrackerHandle(editState.editingTrackers[i]);
		if (tracker)
			tracker->subObjectEditing = 1;
		else
			trackerHandleDestroy(eaRemove(&editState.editingTrackers, i));
	}

	// refresh the default parent
	tracker = trackerFromTrackerHandle(editState.defaultParent);
	if (!tracker)
	{
		wleSetDefaultParent(NULL);
		for (i = 0; i < zmapGetLayerCount(NULL); i++)
		{
			ZoneMapLayer *layer = zmapGetLayer(NULL, i);
			
			if (layer)
			{
				TrackerHandle *handle = trackerHandleCreate(layerGetTracker(layer));
				if (handle)
				{
					wleSetDefaultParent(handle);
					break;
				}
			}
		}
	}

	// refresh the tracker type and gizmo
	wleEdObjTrackerSelectionChanged(edObjSelectionGet(EDTYPE_TRACKER), false);
	eaDestroy(&handles);
}

/******
* This function adds the EditorObject selection as TrackerHandles to the specified EArray.  The handles
* stored in this earray should NOT generally be destroyed, as its memory is being managed in the framework.
* PARAMS:
*   output - TrackerHandle EArray where the selection will be stored
******/
void wleSelectionGetTrackerHandles(TrackerHandle ***output)
{
	EditorObject **edObjs = edObjSelectionGet(EDTYPE_TRACKER);
	int i;
	for (i = 0; i < eaSize(&edObjs); i++)
		eaPush(output, edObjs[i]->obj);
}

/******
* This function returns the number of selected trackers in the world editor.
* RETURNS:
*   int number of trackers currently selected in the world
******/
int wleSelectionGetCount(void)
{
	return edObjSelectionGetCount(EDTYPE_TRACKER);
}

/******
* This function indicates whether a particular tracker (at a specified TrackerHandle) is selected
* or not.
* PARAMS:
*   handle - TrackerHandle to test for selection
* RETURNS:
*   bool indicating whether the tracker at handle is selected
******/
bool wleTrackerIsSelected(const TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	return !!edObjFindSelected(editorObjectCreate(trackerHandleCopy(handle), "", tracker ? tracker->parent_layer : NULL, EDTYPE_TRACKER));
}

/******
* This function is a wrapper to the EditorObject framework and deselects the tracker at the specified TrackerHandle.
* PARAMS:
*   handle - TrackerHandle being deselected
******/
bool wleTrackerDeselect(const TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	return edObjDeselect(editorObjectCreate(trackerHandleCopy(handle), "", tracker ? tracker->parent_layer : NULL, EDTYPE_TRACKER));
}

/******
* This function deselects all trackers
******/
void wleTrackerDeselectAll(void)
{
	edObjSelectionClear(EDTYPE_NONE);
}

/******
* This function is a wrapper to the EditorObject selection harness and handles selecting the
* specified tracker.
* PARAMS:
*   handle - TrackerHandle to select
*   additive - bool indicating whether to add specified tracker to selection or select it exclusively
* RETURNS:
*   bool indicating whether tracker was successfully selected
******/
bool wleTrackerSelect(const TrackerHandle *handle, bool additive)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	return edObjSelect(editorObjectCreate(trackerHandleCopy(handle), tracker ? tracker->def->name_str : "", tracker ? tracker->parent_layer : NULL, EDTYPE_TRACKER), additive, true);
}

/******
* This function is a wrapper to the EditorObject list selection harness and handles selecting the
* specified tracker list.
* PARAMS:
*   handles - TrackerHandle list to select
*   additive - bool indicating whether to add specified tracker to selection or select it exclusively
* RETURNS:
*   bool indicating whether tracker was successfully selected
******/
bool wleTrackerSelectList(const TrackerHandle **handles, bool additive)
{
	int i;
	EditorObject **selection_list = NULL;
	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handles[i]);
		eaPush(&selection_list, editorObjectCreate(trackerHandleCopy(handles[i]), tracker ? tracker->def->name_str : "", tracker ? tracker->parent_layer : NULL, EDTYPE_TRACKER));
	}
	return edObjSelectList(selection_list, additive, true);
	eaDestroy(&selection_list);
}

/******
* This function is a wrapper to the EditorObject selection framework toggle, which toggles the
* selection state of a particular object.  In this case the selection state of a tracker will be toggled.
* PARAMS:
*   handle - TrackerHandle to be toggled
* RETURNS:
*   bool indicating the end selection state of the tracker being toggled
******/
bool wleTrackerSelectToggle(const TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	return edObjSelectToggle(editorObjectCreate(trackerHandleCopy(handle), tracker ? tracker->def->name_str : "", tracker ? tracker->parent_layer : NULL, EDTYPE_TRACKER));
}

/******
* This is the recursion workhorse for selection inversion.  It essentially populates an EArray of new
* trackers to select by adding those trackers that have no currently selected descendants.
* PARAMS:
*   root - GroupTracker where recursion begins
*   newSelection - TrackerHandle EArray of trackers that should be selected after this function is
*                  completely finished running
******/
static void wleSelectionInvertRecurse(GroupTracker *root, TrackerHandle ***newSelection)
{
	TrackerHandle **selection = NULL;
	TrackerHandle *rootHandle = trackerHandleCreate(root);
	int i;
	bool hasSelectedDescendant = false;

	assert(rootHandle);

	// do not recurse past selected trackers
	if (wleTrackerIsSelected(rootHandle))
		return;

	// check to see if the current tracker has any selected descendants
	wleSelectionGetTrackerHandles(&selection);
	for (i = 0; i < eaSize(&selection); i++)
	{
		assert(selection[i]);
		if (wleTrackerIsDescendant(selection[i], rootHandle))
		{
			hasSelectedDescendant = true;
			break;
		}
	}
	eaDestroy(&selection);

	// if it has no selected descendants, the tracker should be selected and recursion ended
	if (!hasSelectedDescendant)
		eaPush(newSelection, trackerHandleCopy(rootHandle));
	// otherwise, recurse to its children
	else
	{
		for (i = 0; i < root->child_count; i++)
			wleSelectionInvertRecurse(root->children[i], newSelection);
	}

	trackerHandleDestroy(rootHandle);
}

/******
* This function inverts the current selection.
******/
void wleSelectionInvert(void)
{
	TrackerHandle **newSelection = NULL;
	int i, j;

	if (editState.lockedSelection)
	{
		wleUISelectionLockWarn();
		return;
	}

	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		GroupTracker *layer = layerGetTracker(zmapGetLayer(NULL, i));
		TrackerHandle *layerHandle;

		if (!layer)
			continue;

		layerHandle = trackerHandleCreate(layer);
		assert(layerHandle);
		// we deselect selected layers
		if (wleTrackerIsSelected(layerHandle))
			wleTrackerDeselect(layerHandle);

		// we ensure that a layer can never be selected by specifying each layer's children as the
		// roots of recursion
		else
		{
			for (j = 0; j < layer->child_count; j++)
				wleSelectionInvertRecurse(layer->children[j], &newSelection);
		}
		trackerHandleDestroy(layerHandle);
	}

	// deselect everything and select our new, inverted tracker selection
	wleTrackerDeselectAll();
	wleTrackerSelectList(newSelection, true);
	eaDestroyEx(&newSelection, trackerHandleDestroy);
}

/******
* This function indicates whether a tracker is being hidden.
* PARAMS:
*   handle - TrackerHandle to check
******/
bool wleTrackerIsHidden(const TrackerHandle *handle)
{
	int i;
	for (i = 0; i < eaSize(&editState.hiddenTrackers); i++)
	{
		if (wleTrackerIsDescendant(handle, editState.hiddenTrackers[i]))
			return true;
	}
	return false;
}

/******
* This function hides a specified tracker.
* PARAMS:
*   handle - the tracker to hide
******/
void wleTrackerHide(const TrackerHandle *handle)
{
	TrackerHandle *copy = trackerHandleCopy(handle);
	GroupTracker *tracker = trackerFromTrackerHandle(copy);
	int i;

	assert(tracker && copy);
	if (wleTrackerIsHidden(handle))
		return;

	// ensure we don't add duplicate trackers to the list and enforce invariance of having none of the
	// trackers in the list be descendants (inclusive) of another in the list
	for (i = eaSize(&editState.hiddenTrackers) - 1; i >= 0; i--)
	{
		if (wleTrackerIsDescendant(editState.hiddenTrackers[i], copy) || wleTrackerIsDescendant(copy, editState.hiddenTrackers[i]))
			wleTrackerUnhide(editState.hiddenTrackers[i]);
	}
	eaPush(&editState.hiddenTrackers, trackerHandleCopy(copy));
	wleTrackerDeselect(copy);
	wleTrackerHideMain(tracker);
	trackerHandleDestroy(copy);
}

/******
* This function unhides a specified tracker.
* PARAMS:
*   handle - the tracker to unhide
******/
void wleTrackerUnhide(const TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	int i;

	for (i = 0; i < eaSize(&editState.hiddenTrackers); i++)
	{
		GroupTracker *hidden  = trackerFromTrackerHandle(editState.hiddenTrackers[i]);
		if (handle == editState.hiddenTrackers[i] || trackerHandleComp(editState.hiddenTrackers[i], handle) == 0)
		{
			trackerHandleDestroy(eaRemove(&editState.hiddenTrackers, i));
			if (tracker)
			{
				trackerSetInvisible(tracker, false);
				if (!tracker->parent)
				{
					// Unhiding the scratch layer should update the UI
					if(tracker->parent_layer->scratch && editorUIState && !editorUIState->showingScratchLayer) {
						editorUIState->showingScratchLayer = true;
						wleToolbarUpdateEditingScratchLayer(tracker->parent_layer);
					}
					for (i = 0; i < eaSize(&wleMapLayers); i++)
					{
						if (tracker->parent_layer == emMapLayerGetUserPtr(wleMapLayers[i]))
							emMapLayerSetVisible(wleMapLayers[i], 1);
					}
				}
				wleUITrackerTreeRefresh(NULL);
				wleAERefresh();
			}
			break;
		}
		else if (tracker && hidden && wleTrackerIsDescendant(handle, editState.hiddenTrackers[i]))
		{
			// if we are unhiding a descendant instead of the actual hidden tracker, we need
			// to unhide the parent and hide the children that AREN'T ancestors of the tracker
			// being hidden
			int j;

			trackerSetInvisible(tracker, false);
			trackerUpdate(hidden, hidden->def, true);
			tracker = trackerFromTrackerHandle(handle);
			assert(tracker);
			trackerHandleDestroy(eaRemove(&editState.hiddenTrackers, i));
			wleUITrackerTreeRefresh(NULL);
			wleAERefresh();
			while (tracker->parent != hidden)
				tracker = tracker->parent;
			for (j = 0; j < hidden->child_count; j++)
			{
				if (j != tracker->idx_in_parent)
				{
					TrackerHandle *child = trackerHandleCreate(hidden->children[j]);
					wleTrackerHide(child);
					trackerHandleDestroy(child);
				}
			}
			break;
		}
	}
}

/******
* This toggles the hidden state of a particular tracker.
* PARAMS:
*   handle - TrackerHandle to toggle
******/
void wleTrackerToggleHide(const TrackerHandle *handle)
{
	if (wleTrackerIsHidden(handle))
		wleTrackerUnhide(handle);
	else
		wleTrackerHide(handle);
}

/******
* This function unhides all hidden trackers.
******/
void wleTrackerUnhideAll(void)
{
	int i;
	for (i = eaSize(&editState.hiddenTrackers) - 1; i >= 0; i--) {
		GroupTracker *tracker = trackerFromTrackerHandle(editState.hiddenTrackers[i]);
		// We don't want unhide all to expose the scratch layer if it is hidden
		if(tracker && !tracker->parent && tracker->parent_layer && tracker->parent_layer->scratch && editorUIState && !editorUIState->showingScratchLayer)
			continue;
		wleTrackerUnhide(editState.hiddenTrackers[i]);
	}
}

/******
* This function indicates whether a specified tracker is frozen.  A frozen tracker is defined as
* being any tracker that is a descendant (inclusive) of a tracker in the editState's frozen tracker
* list.
* PARAMS:
*   handle - TrackerHandle to check for frozen state
* RETURNS:
*   bool indicating whether tracker is frozen or not
******/
bool wleTrackerIsFrozen(const TrackerHandle *handle)
{
	int i;
	for (i = 0; i < eaSize(&editState.frozenTrackers); i++)
	{
		if (wleTrackerIsDescendant(handle, editState.frozenTrackers[i]))
			return true;
	}
	return false;
}

/******
* This function freezes a specified tracker.
* PARAMS:
*   handle - the tracker to freeze
******/
void wleTrackerFreeze(const TrackerHandle *handle)
{
	TrackerHandle *copy = trackerHandleCopy(handle);
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	int i;
	assert(tracker && copy);

	// ensure we don't add duplicate trackers to the list and enforce invariance of having none of the
	// trackers in the list be descendants (inclusive) of another in the list
	for (i = eaSize(&editState.frozenTrackers) - 1; i >= 0; i--)
	{
		if (wleTrackerIsDescendant(editState.frozenTrackers[i], copy) || wleTrackerIsDescendant(copy, editState.frozenTrackers[i]))
			wleTrackerUnfreeze(editState.frozenTrackers[i]);
	}
	eaPush(&editState.frozenTrackers, trackerHandleCopy(handle));
	wleTrackerDeselect(copy);
	tracker->frozen = 1;
	wleUITrackerTreeRefresh(NULL);
	trackerHandleDestroy(copy);
}

/******
* This function unfreezes a specified tracker.
* PARAMS:
*   handle - the tracker to unfreeze
******/
void wleTrackerUnfreeze(const TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	int i;

	for (i = 0; i < eaSize(&editState.frozenTrackers); i++)
	{
		GroupTracker *frozen  = trackerFromTrackerHandle(editState.frozenTrackers[i]);
		if (handle == editState.frozenTrackers[i] || trackerHandleComp(editState.frozenTrackers[i], handle) == 0)
		{
			if (tracker)
				tracker->frozen = 0;
			eaRemove(&editState.frozenTrackers, i);
			wleUITrackerTreeRefresh(NULL);
			break;
		}
		else if (tracker && frozen && wleTrackerIsDescendant(handle, editState.frozenTrackers[i]))
		{
			// if we are unfreezing a descendant instead of the actual frozen tracker, we need
			// to unfreeze the ancestor and hide the children that AREN'T ancestors of the tracker
			// being hidden
			int j;

			frozen->frozen = 0;
			eaRemove(&editState.frozenTrackers, i);
			wleUITrackerTreeRefresh(NULL);
			while (tracker->parent != frozen)
				tracker = tracker->parent;
			for (j = 0; j < frozen->child_count; j++)
			{
				if (j != tracker->idx_in_parent)
				{
					TrackerHandle *child = trackerHandleCreate(frozen->children[j]);
					wleTrackerFreeze(child);
					trackerHandleDestroy(child);
				}
			}
			break;
		}
	}
}

/******
* This toggles the hidden state of a particular tracker.
* PARAMS:
*   handle - TrackerHandle to toggle
******/
void wleTrackerToggleFreeze(const TrackerHandle *handle)
{
	if (wleTrackerIsFrozen(handle))
		wleTrackerUnfreeze(handle);
	else
		wleTrackerFreeze(handle);
}

/******
* This function unfreezes all frozen trackers.
******/
void wleTrackerUnfreezeAll(void)
{
	int i;
	for (i = eaSize(&editState.frozenTrackers) - 1; i >= 0; i--)
		wleTrackerUnfreeze(editState.frozenTrackers[i]);
}

void wleTrackerUndoToggleSubObject(void *context, TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	bool wasEditing = false;
	int i;

	if (!tracker || !tracker->def || (!tracker->def->property_structs.curve && !tracker->def->property_structs.patrol_properties))
		return;
	for (i = 0; i < eaSize(&editState.editingTrackers); i++)
	{
		if (trackerHandleComp(editState.editingTrackers[i], handle) == 0)
		{
			wasEditing = true;
			tracker->subObjectEditing = 0;
			trackerHandleDestroy(editState.editingTrackers[i]);
			eaRemove(&editState.editingTrackers, i);
			break;
		}
	}
	if (!wasEditing)
	{
		tracker->subObjectEditing = 1;
		eaPush(&editState.editingTrackers, trackerHandleCopy(handle));
	}
	wleUITrackerTreeRefresh(NULL);
}

void wleTrackerUndoFree(void *context, TrackerHandle *handle)
{
	trackerHandleDestroy(handle);
}

/******
* This toggles the subobject editing state of the tracker.
* PARAMS:
*   handle - TrackerHandle to toggle
******/
void wleTrackerToggleEditSubObject(const TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);

	if (!tracker || !tracker->def || (!tracker->def->property_structs.curve && !tracker->def->property_structs.patrol_properties))
		return;

	tracker->subObjectEditing = !tracker->subObjectEditing;
	EditCreateUndoCustom(edObjGetUndoStack(), wleTrackerUndoToggleSubObject, wleTrackerUndoToggleSubObject, wleTrackerUndoFree, trackerHandleCopy(handle));
	if (tracker->subObjectEditing)
		eaPush(&editState.editingTrackers, trackerHandleCopy(handle));
	else
	{
		int i;
		for (i = 0; i < eaSize(&editState.editingTrackers); i++)
		{
			if (trackerHandleComp(editState.editingTrackers[i], handle) == 0)
			{
				trackerHandleDestroy(editState.editingTrackers[i]);
				eaRemove(&editState.editingTrackers, i);
				break;
			}
		}
	}
	wleUITrackerTreeRefresh(NULL);
}

EditorObject *wlePatrolPointEdObjCreate(const TrackerHandle *handle, int pointIdx)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	char name[32];

	if (tracker && tracker->def && tracker->def->property_structs.patrol_properties && pointIdx >= 0 && pointIdx < eaSize(&tracker->def->property_structs.patrol_properties->patrol_points))
	{
		sprintf(name, "Point %i", pointIdx);
		return editorObjectCreate(wleEncObjSubHandleCreate(handle, pointIdx), name, tracker->parent_layer, EDTYPE_PATROL_POINT);
	}

	return NULL;
}

EditorObject *wleEncounterActorEdObjCreate(const TrackerHandle *handle, int actorIdx)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);

	if (tracker && tracker->def && tracker->def->property_structs.encounter_properties && actorIdx >= 0 && actorIdx < eaSize(&tracker->def->property_structs.encounter_properties->eaActors))
		return editorObjectCreate(wleEncObjSubHandleCreate(handle, actorIdx), tracker->def->property_structs.encounter_properties->eaActors[actorIdx]->pcName, tracker->parent_layer, EDTYPE_ENCOUNTER_ACTOR);

	return NULL;
}

void wlePatrolPointSelect(const TrackerHandle *handle, int pointIdx, bool additive, bool expandToNode)
{
	EditorObject *patrolPointEdObj = wlePatrolPointEdObjCreate(handle, pointIdx);

	if (patrolPointEdObj)
		edObjSelect(patrolPointEdObj, additive, expandToNode);
}

void wleEncounterActorSelect(const TrackerHandle *handle, int actorIdx, bool additive, bool expandToNode)
{
	EditorObject *encounterActorEdObj = wleEncounterActorEdObjCreate(handle, actorIdx);

	if (encounterActorEdObj)
		edObjSelect(encounterActorEdObj, additive, expandToNode);
}

/******
* EDIT STATE MANAGEMENT
******/
/******
* This function sets the editor's default creation parent - where new objects will end up by
* default.
* PARAMS:
*   handle - TrackerHandle of the parent tracker to set as the default
******/
void wleSetDefaultParent(const TrackerHandle *handle)
{
	if (editState.defaultParent)
		trackerHandleDestroy(editState.defaultParent);
	editState.defaultParent = trackerHandleCopy(handle);
}

/******
* This function sets the current gizmo mode (between editing pivots and editing the objects
* themselves.
* PARAMS:
*   mode - EditGizmoMode to set the editor into
******/
void wleSetGizmoMode(EditGizmoMode mode)
{
	editState.gizmoMode = mode;

	if (mode == EditPivot || mode == EditPivotTemp)
		edObjHarnessSetPivotMode(EdObjEditPivot);
	else
		edObjHarnessSetPivotMode(EdObjEditActual);

	if (mode == EditPivot)
	{
		if (editState.tempPivotModified && wleSelectionGetCount() == 1)
		{
			Mat4 finalMat;

			// TODO: make confirmation dialog of saving temporary pivot
			// commit the current matrix as the selection's temporary pivot
			editState.gizmoMode = mode;
			TranslateGizmoGetMatrix(edObjHarnessGetTransGizmo(), finalMat);
			edObjSetMatrix(edObjSelectionGet(EDTYPE_TRACKER)[0], finalMat);
			wleEdObjTrackerEndMove(edObjSelectionGet(EDTYPE_TRACKER));
		}
		editState.tempPivotModified = false;
	}

	wleUIGizmoModeRefresh();
}

/******
* This function sets the special snap mode of the translate gizmo.
* PARAMS:
*   mode - the EditSpecialSnapMode value
******/
void wleSetTranslateSnapMode(EditSpecialSnapMode mode)
{
	TranslateGizmo *transGizmo = edObjHarnessGetTransGizmo();
	EditSpecialSnapMode oldMode = TranslateGizmoGetSpecSnap(transGizmo);
	TranslateGizmoSetSpecSnap(transGizmo, mode);
	if (mode != EditSnapNone && mode != EditSnapGrid && (oldMode == EditSnapNone || oldMode == EditSnapGrid))
		TranslateGizmoDisableAxes(transGizmo);
	wleGizmosUITransGizmoChanged(transGizmo);
}


/******
* OPERATION WRAPPERS
******/
static void wleObjectPlaceEx(WleCopyBuffer **buffer, bool noReplace)
{
	// immediately perform placement/replacement if there is a selection
	if (wleSelectionGetCount() > 0 && !noReplace)
	{
		TrackerHandle **handles = NULL;
		TrackerHandle *parent;
		GroupDef **defs = NULL;
		Mat4 *mats = NULL;
		Mat4 pivotMat;
		int count = 0, maxCount = 0;
		int i, j;

		wleSelectionGetTrackerHandles(&handles);
		dynArrayReserveStructs(mats, maxCount, eaSize(&buffer));
		for (i = 0; i < eaSize(&buffer); i++)
		{
			if (buffer[i]->pti == parse_WleTrackerBuffer)
			{
				WleTrackerBuffer *trackerBuffer = buffer[i]->data;
				ZoneMapLayer *layer = trackerBuffer->layerName ? zmapGetLayerByName(NULL, trackerBuffer->layerName) : NULL;
				GroupDefLib *def_lib = layerGetGroupDefLib(layer);
				GroupDef *def;

				if (def_lib)
					def = groupLibFindGroupDef(def_lib, trackerBuffer->uid, false);
				else
					def = objectLibraryGetGroupDef(trackerBuffer->uid, true);

				if (def)
				{
					dynArrayAddStruct(mats, count, maxCount);
					copyMat4(buffer[i]->relMat, mats[count - 1]);
					eaPush(&defs, def);
				}
			}
		}

		// different behaviors for placing with a selection, depending on the replaceOnCreate and repeatCreateAcrossSelection bits
		if (editState.repeatCreateAcrossSelection)
		{
			TrackerHandle **resultHandles = NULL;
			TrackerHandle **tempHandles = NULL;
			Mat4 **tempMats = calloc(eaSize(&handles), sizeof(void*));
			int *tempCount = NULL, *tempMaxCount = NULL, *matCount = NULL;
			int *indices = NULL;
			TrackerHandle **parents = NULL;
			EditUndoBeginGroup(edObjGetUndoStack());
			for (i = 0; i < eaSize(&handles); i++)
			{
				eaiPush(&tempMaxCount, 0);
				eaiPush(&matCount, 0);
				eaiPush(&tempCount, 0);
				tempMats[i] = calloc(1, sizeof(Mat4));
				dynArrayReserveStructs(tempMats[i], tempMaxCount[i], matCount[i]);
				dynArrayAddStructs(tempMats[i], tempCount[i], tempMaxCount[i], matCount[i]);

				// place new objects relative to each selected object
				trackerGetMat(trackerFromTrackerHandle(handles[i]), pivotMat);
				for (j = 0; j < count; j++)
				{
					mulMat4(pivotMat, mats[j], tempMats[i][j]);
				}

				eaClear(&tempHandles);
				eaPush(&tempHandles, handles[i]);
				parent = wleFindCommonParent(tempHandles);
				if (!parent)
				{
					eaPush(&parents, NULL);	//A spacer, to keep i consistent
					continue;
				}
				ANALYSIS_ASSUME(parent);
				eaPush(&parents, parent);
				if (!editState.replaceOnCreate)
				{
					eaiPush(&indices, wleFindCommonParentIndex(parent, tempHandles));
				}
			}

			if (parents)
			{
				if (editState.replaceOnCreate)
				{
					wleOpReplaceEach(parents, handles, defs, tempMats, &resultHandles, false);
				}
				else
				{
					wleOpCreateList(parents, defs, tempMats, NULL, indices, &resultHandles, false);
				}
			}

			for (i = 0; i < eaSize(&parents); i++)
			{
				if (parents[i])
				{
					trackerHandleDestroy(parents[i]);
				}
			}

			// select entire creation
			wleTrackerDeselectAll();
			wleTrackerSelectList(resultHandles, true);
			EditUndoEndGroup(edObjGetUndoStack());

			// cleanup
			eaDestroyEx(&resultHandles, trackerHandleDestroy);
			eaDestroy(&tempHandles);
			for (i = 0; i < eaSize(&handles); i++)
			{
				SAFE_FREE(tempMats[i]);
			}
			SAFE_FREE(tempMats);
			eaiDestroy(&tempMaxCount);
			eaiDestroy(&matCount);
			eaiDestroy(&tempCount);
		}
		else
		{
			// place new objects relative to the gizmo matrix
			edObjHarnessGetGizmoMatrix(pivotMat);
			for (i = 0; i < count; i++)
			{
				Mat4 tempMat;
				copyMat4(mats[i], tempMat);
				mulMat4(pivotMat, tempMat, mats[i]);
			}

			parent = wleFindCommonParent(handles);
			if (!parent)
				parent = trackerHandleCopy(editState.defaultParent);
			if (!parent)
				emStatusPrintf("Placement failed; no default parent is set.");
			else if (editState.replaceOnCreate)
				wleOpReplaceEx(parent, handles, defs, mats, NULL);
			else
			{
				int index = wleFindCommonParentIndex(parent, handles);
				wleOpCreateEx(parent, defs, mats, NULL, index, NULL);
			}
			trackerHandleDestroy(parent);
		}

		eaDestroy(&defs);
		eaDestroy(&handles);
		SAFE_FREE(mats);
	}
	// ...otherwise, go into placement mode
	else
	{
		if (!editState.defaultParent)
			emStatusPrintf("Placement failed; no default parent is set.");
		else
		{
			wleChangeMode(EditNormal);
			editState.quickPlaceState.buffer = wleCopyBufferDup(buffer);
			wleChangeMode(EditPlaceObjects);
		}
	}
}

/******
* This function places the specified object library piece into the map.
* PARAMS:
*   uid - int UID of the object library piece to place
******/
void wleObjectPlace(int uid)
{
	WleCopyBuffer **buffers = NULL;
	WleCopyBuffer *buffer = calloc(1, sizeof(*buffer));
	WleTrackerBuffer *trackerBuffer = StructCreate(parse_WleTrackerBuffer);

	editState.quickPlaceState.objectLibraryClicked = true;

	trackerBuffer->uid = uid;
	buffer->data = trackerBuffer;
	buffer->pti = parse_WleTrackerBuffer;
	identityMat4(buffer->relMat);
	eaPush(&buffers, buffer);

	wleObjectPlaceEx(buffers, false);

	wleCopyBufferClear(buffers);
}

/******
* This function places a patrol point into the map into the specified tracker's patrol.
* PARAMS:
*   parentHandle - TrackerHandle to the patrol to which the point will be added
******/
void wlePatrolPointPlace(const TrackerHandle *parentHandle)
{
	WleCopyBuffer **buffers = NULL;
	WleCopyBuffer *buffer = calloc(1, sizeof(*buffer));
	WlePatrolPointBuffer *pointBuffer = StructCreate(parse_WlePatrolPointBuffer);

	pointBuffer->parentHandle = StructClone(parse_TrackerHandle, parentHandle);
	pointBuffer->properties = NULL;
	buffer->data = pointBuffer;
	buffer->pti = parse_WlePatrolPointBuffer;
	identityMat4(buffer->relMat);
	eaPush(&buffers, buffer);

	wleObjectPlaceEx(buffers, false);

	wleCopyBufferClear(buffers);
}

/******
* This function places an actor into the map into the specified tracker's encounter.
* PARAMS:
*   parentHandle - TrackerHandle to the encounter to which the actor will be added
******/
void wleEncounterActorPlace(const TrackerHandle *parentHandle)
{
	WleCopyBuffer **buffers = NULL;
	WleCopyBuffer *buffer = calloc(1, sizeof(*buffer));
	WleEncounterActorBuffer *actorBuffer = StructCreate(parse_WleEncounterActorBuffer);

	actorBuffer->parentHandle = StructClone(parse_TrackerHandle, parentHandle);
	actorBuffer->properties = NULL;
	buffer->data = actorBuffer;
	buffer->pti = parse_WleEncounterActorBuffer;
	identityMat4(buffer->relMat);
	eaPush(&buffers, buffer);

	wleObjectPlaceEx(buffers, false);

	wleCopyBufferClear(buffers);
}

void wleEdObjSelectionPlace(void)
{
	WleCopyBuffer **buffers = NULL;
	WleCopyBuffer *buffer = calloc(1, sizeof(*buffer));
	Mat4 gizmoMat;

	buffer->data = buffer->pti = NULL;
	buffer->useCurrentSelection = true;
	eaPush(&buffers, buffer);

	edObjHarnessGetGizmoMatrix(gizmoMat);

	wleObjectPlaceEx(buffers, true);
	wleCopyBufferClear(buffers);

	// modify the quick place pivot to start with the gizmo's current rotation
	copyMat3(gizmoMat, editState.quickPlaceState.pivotMat);
}

/******
* This function deals with snapping an existing tracker's subobjects along the y-axis.
* PARAMS:
*   handles - TrackerHandle EArray of trackers whose subobjects are to be snapped along the y-axis.
******/
static void wleTrackersSnapSubObjects(const TrackerHandle **handles)
{
	int i, j;

	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handles[i]);
		GroupDef *def = tracker ? tracker->def : NULL;
		Mat4 mat;

		if (groupIsObjLib(def))
			continue; // Only snap instanced objects

		if (tracker)
			trackerGetMat(tracker, mat);

		// snap patrol points along y-axis in placement mode
		if (def && def->property_structs.patrol_properties && eaSize(&def->property_structs.patrol_properties->patrol_points))
		{
			WleEncObjSubHandle **subHandles = NULL;
			Vec3 *vecs = calloc(eaSize(&def->property_structs.patrol_properties->patrol_points), sizeof(Vec3));

			wlePatrolPathRefreshMat(def->property_structs.patrol_properties, mat, true, false);
			for (j = 0; j < eaSize(&def->property_structs.patrol_properties->patrol_points); j++)
			{
				eaPush(&subHandles, wleEncObjSubHandleCreate(handles[i], j));
				copyVec3(def->property_structs.patrol_properties->patrol_points[j]->draw_mat[3], vecs[j]);
			}
			wleOpMovePatrolPoints(subHandles, vecs);
			eaDestroyEx(&subHandles, wleEncObjSubHandleDestroy);
			SAFE_FREE(vecs);
		}

		// snap actors along y-axis in placement mode
		if (def && def->property_structs.encounter_properties && eaSize(&def->property_structs.encounter_properties->eaActors))
		{
			WleEncObjSubHandle **subHandles = NULL;
			Mat4 *actorMats = calloc(eaSize(&def->property_structs.encounter_properties->eaActors), sizeof(Mat4));

			for (j = 0; j < eaSize(&def->property_structs.encounter_properties->eaActors); j++)
			{
				wleEncounterActorRefreshMat(def->property_structs.encounter_properties->eaActors[j], mat, true);
				eaPush(&subHandles, wleEncObjSubHandleCreate(handles[i], j));
				copyMat4(def->property_structs.encounter_properties->eaActors[j]->draw_mat, actorMats[j]);
			}
			wleOpMoveEncounterActors(subHandles, actorMats);
			eaDestroyEx(&subHandles, wleEncObjSubHandleDestroy);
			SAFE_FREE(actorMats);		
		}
	}
}

/******
* This function simulates manipulating the transformation gizmo(s) to the specified matrix.  It
* is a wrapper to the "end move" callback used with the EditorObject selection harness to finalize
* a move operation.  In short, this function moves a specified tracker to the specified matrix.
* PARAMS:
*   handle - TrackerHandle being moved
*   mat - Mat4 destination
*   snapSubObjsToY - bool indicating whether to snap the trackers' subobjects along the Y-axis
******/
void wleTrackersMoveToMats(const TrackerHandle **handles, const Mat4 *mats, bool snapSubObjsToY)
{
	TrackerHandle **allHandles = NULL;
	WleEncObjSubHandle **actorSubHandles = NULL;
	WleEncObjSubHandle **patrolPointSubHandles = NULL;
	Mat4 *allMats = NULL;
	Mat4 *actorMats= NULL;
	Vec3 *patrolPointVecs = NULL;
	int i, j;
	int allMatsCount, allMatsMax;
	int patrolPointVecsCount, patrolPointVecsMax, actorMatsCount, actorMatsMax;

	allMatsCount = allMatsMax = 0;
	patrolPointVecsCount = patrolPointVecsMax = actorMatsCount = actorMatsMax = 0;

	// if moving the pivots
	if (editState.gizmoMode == EditPivot && editState.mode != EditPlaceObjects)
	{
		for (i = 0; i < eaSize(&handles); i++)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(handles[i]);
			Mat4 trackerMat;
			int startMatCount = allMatsCount;
			int startPatrolPointVecsCount = patrolPointVecsCount;
			int startActorMatsCount = actorMatsCount;

			assert(tracker);
			trackerGetMat(tracker, trackerMat);

			// move tracker to target location and keep tracker's children fixed
			eaPush(&allHandles, trackerHandleCreate(tracker));
			for (j = 0; j < tracker->child_count; j++)
			{
				GroupTracker *child = tracker->children[j];
				eaPush(&allHandles, trackerHandleCreate(child));
			}
			dynArrayAddStructs(allMats, allMatsCount, allMatsMax, tracker->child_count + 1);
			copyMat4(mats[i], allMats[startMatCount]);
			for (j = startMatCount + 1; j < allMatsCount; j++)
				trackerGetMat(trackerFromTrackerHandle(allHandles[j]), allMats[j]);

			// if tracker is a patrol, keep its points in place
			if (tracker->def && tracker->def->property_structs.patrol_properties && eaSize(&tracker->def->property_structs.patrol_properties->patrol_points) > 0)
			{
				WorldPatrolProperties *patrolProperties = tracker->def->property_structs.patrol_properties;
				dynArrayAddStructs(patrolPointVecs, patrolPointVecsCount, patrolPointVecsMax, eaSize(&patrolProperties->patrol_points));
				for (j = 0; j < eaSize(&patrolProperties->patrol_points); j++)
				{
					eaPush(&patrolPointSubHandles, wleEncObjSubHandleCreate(handles[i], j));
					mulVecMat4(patrolProperties->patrol_points[j]->pos, trackerMat, patrolPointVecs[startPatrolPointVecsCount + j]);
				}
			}

			// if tracker is an encounter, keep its actors in place
			if (tracker->def && tracker->def->property_structs.encounter_properties && eaSize(&tracker->def->property_structs.encounter_properties->eaActors) > 0)
			{
				WorldEncounterProperties *encounterProperties = tracker->def->property_structs.encounter_properties;
				dynArrayAddStructs(actorMats, actorMatsCount, actorMatsMax, eaSize(&encounterProperties->eaActors));
				for (j = 0; j < eaSize(&encounterProperties->eaActors); j++)
				{
					Mat4 actorMat;
					eaPush(&actorSubHandles, wleEncObjSubHandleCreate(handles[i], j));
					createMat3YPR(actorMat, encounterProperties->eaActors[j]->vRot);
					copyVec3(encounterProperties->eaActors[j]->vPos, actorMat[3]);
					mulMat4(trackerMat, actorMat, actorMats[startActorMatsCount + j]);
				}
			}
		}
	}
	// if moving the objects themselves
	else if (eaSize(&handles) > 0)
	{
		dynArrayAddStructs(allMats, allMatsCount, allMatsMax, eaSize(&handles));
		for (i = 0; i < eaSize(&handles); i++)
		{
			copyMat4(mats[i], allMats[i]);
			eaPush(&allHandles, trackerHandleCopy(handles[i]));
		}
	}

	// perform move operation; in case of failure, reset the matrix position
	EditUndoBeginGroup(edObjGetUndoStack());
	if (!wleOpMove(allHandles, allMats))
	{
		wleRefreshState();
		wleGizmoUpdateMatrix();
		wleAERefresh();
	}
	else
	{
		if (eaSize(&patrolPointSubHandles) > 0)
			wleOpMovePatrolPoints(patrolPointSubHandles, patrolPointVecs);
		if (eaSize(&actorSubHandles) > 0)
			wleOpMoveEncounterActors(actorSubHandles, actorMats);
	}
	EditUndoEndGroup(edObjGetUndoStack());

	eaDestroyEx(&allHandles, trackerHandleDestroy);
	eaDestroyEx(&patrolPointSubHandles, wleEncObjSubHandleDestroy);
	eaDestroyEx(&actorSubHandles, wleEncObjSubHandleDestroy);
	SAFE_FREE(allMats);
	SAFE_FREE(patrolPointVecs);
	SAFE_FREE(actorMats);
}

/******
* This function takes an object library piece and deletes it permanently from the object library.
* PARAMS:
*   ole - ResourceInfo corresponding to the piece to delete
******/
void wleDeleteFromLib(ResourceInfo *ole)
{
	emStatusPrintf("Deletion from library is under reevaluation; see Joseph if this is urgent.");
	// TODO: renable this after redesigning the deletion process
	/*
	GroupFile *file = objlibFindGroupFile(ole->filename);
	GroupDef **containerDefs = NULL;
	GroupDef *olDef;

	assert(file && gfileGetLockOwner(file) == 3);
	olDef = gfileFindGroupDef(file, ole->uid);
	if (!olDef)
	amStatusPrintf("Cannot find the definition!");
	else if (countObjLibReferences(olDef, &containerDefs) > 0)
	wleUIDeleteFromLibError(olDef, containerDefs);
	else
	wleUIDeleteFromLibConfirm(olDef);
	*/
}

/******
* This function creates an autosave suffix string, depending on the files that already exist.  It
* creates a suffix of the form "<num from 0-9>.autosave", using the first number for which a file
* does not already exist.  If all of the numbers are taken, this overwrites the oldest file.
* PARAMS:
*   basename - string name of the base file name and where the suffix-appended name will be stored
*   size - int buffer length of basename
******/
static void wleFindAutosaveSuffix(char *basename, int size)
{
	char *nameStart;
	char justName[MAX_PATH];
	char newBasename[MAX_PATH];
	char filename[MAX_PATH];
	int i;
	__time32_t oldestTime = 0;
	int oldestTimeIdx = -1;

	strcpy(newBasename, basename);
	nameStart = getFileName(newBasename);
	strcpy(justName, nameStart);
	nameStart[0] = '\0';
	strcat(newBasename, "AutoSaves/");
	strcat(newBasename, justName);

	for (i = 0; i < MAX_AUTOSAVES; i++)
	{
		strcpy(filename, newBasename);
		strcatf(filename, ".%i.autosave", i);
		if (!fileExists(filename))
		{
			strcpy_s(basename, size, filename);
			return;
		}
		else
		{
			__time32_t currTime = fileLastChanged(filename);
			if (!i)
			{
				oldestTime = currTime;
				oldestTimeIdx = i;
			}
			else if (currTime < oldestTime)
			{
				oldestTime = currTime;
				oldestTimeIdx = i;
			}
		}
	}

	if (oldestTimeIdx >= 0)
	{
		strcpy(filename, newBasename);
		strcatf(filename, ".%i.autosave", oldestTimeIdx);
		strcpy_s(basename, size, filename);
	}
}

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct ZoneMapInfoWrapper
{
	ZoneMapInfo **zmap;				AST(NAME(ZoneMap))
} ZoneMapInfoWrapper;
extern ParseTable parse_ZoneMapInfoWrapper[];
#define TYPE_parse_ZoneMapInfoWrapper ZoneMapInfoWrapper

/******
* This function performs a total client-side save.  This means that all files currently locked
* by the client (as well as the zonemap file) are saved locally without any server
* communication.  This is useful in case the server dies.  It can be performed either as an autosave
* (where up to 10 autosaved versions are stored on disk) or as a normal client save (where only
* one is stored and is easy to distinguish from the autosaves).  Autosaved files have the filename
* "<original filename>.<int from 0-9>.autosave", and client saves look like
* "<original filename>.client.autosave".
* PARAMS:
*   autosave - bool determining whether to save with the numeric (autosave) suffix or ".client" suffix
* NOTE: Terrain files are not auto-saved
******/
void wleClientSave(bool autosave)
{
	int i;
	char filename[MAX_PATH];
	ZoneMapInfoWrapper wrapper = { 0 };

	// save zone map
	strcpy(filename, zmapGetFilename(NULL));
	if (autosave)
		wleFindAutosaveSuffix(SAFESTR(filename));
	else
		strcat(filename, ".client.autosave");
	eaPush(&wrapper.zmap, zmapGetInfo(worldGetPrimaryMap()));
	if (!ParserWriteTextFile(filename, parse_ZoneMapInfoWrapper, &wrapper, 0, 0))
	{
		if (!autosave)
			emStatusPrintf("Could not save zone map locally!");
	}
	else
		printf("SAVED \"%s\"\n", filename);
	eaDestroy(&wrapper.zmap);

	// save off each locked layer
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer && (layerGetMode(layer) == LAYER_MODE_EDITABLE) && !layer->saving)
		{
			strcpy(filename, layerGetFilename(layer));
			if (autosave)
				wleFindAutosaveSuffix(SAFESTR(filename));
			else
				strcat(filename, ".client.autosave");
			// TomY this shouldn't save the actual terrain source
			if (!layerSaveAs(layer, filename, true, false, false, true))
			{
				if (!autosave)
					emStatusPrintf("Could not save layer \"%s\" locally!", layerGetFilename(layer));
			}
			else
				printf("SAVED \"%s\"\n", filename);
		}
	}

	if (!autosave)
	{
		emStatusPrintf("Saved files on client.");
		if (wl_state.save_map_game_callback)
		{
			wl_state.save_map_game_callback(NULL);
		}
	}
	printf("CLIENTSAVE COMPLETE\n");
}

/******
* EDITOR AUTO COMMANDS
*
* Each command is preceded by a check function (if applicable).  The check function is used by the
* menu system in order to determine whether to enable the menu item that corresponds to the command.
* The check function must also be called within the command itself in case the command is invoked
* directly from the console.
******/

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.GenerateObjLibUID");
void wleCmdGenerateObjLibUID(void)
{
	int uid = worldGenerateDefNameUID("Seed", NULL, true, strcmpi(GetShortProductName(), SHORT_PRODUCT_NAME_UNSPECIFIED) == 0);
	emStatusPrintf("UID: %d", uid);
	printf("UID: %d\n", uid);
};

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.GenerateCoreObjLibUID");
void wleCmdGenerateCoreObjLibUID(void)
{
	int uid = worldGenerateDefNameUID("Seed", NULL, true, true);
	emStatusPrintf("UID: %d", uid);
	printf("UID: %d\n", uid);
};

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ActivateBoreSelect");
void wleCmdActivateBoreSelect(bool enable)
{
	editState.inputData.boreKey = enable;
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ActivateMoveCopy");
void wleCmdActivateMoveCopy(bool enable)
{
	editState.inputData.moveCopyKey = enable;

	if (editState.drawGhosts)
	{
		EditorObject **selection;
		int i;

		// we render the moving objects visible/invisible if move
		selection = edObjSelectionGet(EDTYPE_TRACKER);
		for (i = 0; i < eaSize(&selection); i++)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(selection[i]->obj);
			if (editState.drawGhosts)
				trackerSetInvisible(tracker, !enable);
		}
	}
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ToggleEditingScrachLayer");
void wleCmdToggleEditingScratchLayer(void)
{
	wleToolbarToggleEditingScratchLayer();
}

// A good default check function
bool wleCheckDefault(UserData unused)
{
	return (worldEditor.inited && !wleGizmoIsActive() && (editState.mode == EditNormal || editState.mode == EditPlaceObjects) && !wleIsViewingGraphic());
}

bool wleCheckCmdCutCopyPaste(UserData unused)
{
	return wleCheckDefault(NULL);
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.SelectDo");
void wleCmdSelectDo(const char *param)
{
	// TomY TODO - convert param to lowercase
	if (!param)
		return;
	printf("Selection do: %s\n", param);
	editorObjectActionDispatch(param);
}

#define WLE_ENTRY_COMPARE_TOL 0.0000001f
static bool wleCmdPopupMenuCompareEntries(WorldCollisionEntry *collEntry, WorldDrawableEntry *drawEntry)
{
	return (nearSameMat4Tol(collEntry->base_entry.bounds.world_matrix, drawEntry->base_entry.bounds.world_matrix, WLE_ENTRY_COMPARE_TOL, WLE_ENTRY_COMPARE_TOL) &&
		nearSameVec3Tol(collEntry->base_entry.shared_bounds->local_min, drawEntry->base_entry.shared_bounds->local_min, WLE_ENTRY_COMPARE_TOL) &&
		nearSameVec3Tol(collEntry->base_entry.shared_bounds->local_max, drawEntry->base_entry.shared_bounds->local_max, WLE_ENTRY_COMPARE_TOL));
}

static WorldDrawableEntry *wleCmdPopupMenuFindDrawable(WorldCollisionEntry *collEntry)
{
	WorldCellEntry *baseEntry = &collEntry->base_entry;
	WorldCell *cell = collEntry->base_entry_data.cell;
	WorldDrawableEntry *ret = NULL;
	int i;

	while (cell && !ret)
	{
		WorldDrawableEntry **cellEntries = NULL;
		worldCellGetDrawableEntries(cell, &cellEntries);
		for (i = 0; i < eaSize(&cellEntries) && !ret; i++)
		{
			if (wleCmdPopupMenuCompareEntries(collEntry, cellEntries[i]))
				ret = cellEntries[i];
		}

		cell = worldCellGetParent(cell);
		eaDestroy(&cellEntries);
	}

	return ret;
}

static int wleCmdPopupMenuTextureCmp(const BasicTexture **texture1, const BasicTexture **texture2)
{
	return strcmp((*texture1)->name, (*texture2)->name);
}

// Popup custom/context menu
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.PopupMenu");
void wleCmdPopupMenu(void)
{
	EditorObject **selection = NULL;
	bool hideDisabled = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "HideDisabledItems", 0);

	edObjSelectionGetAll(&selection);
	eaForEach(&selection, editorObjectRef);
	if (eaSize(&selection) > 0)
		edObjMenuPopupAtCursor(wleMenuRightClick, selection, hideDisabled);
	else
	{
		if (editState.rayCollideInfo.results->hitSomething && wcoGetUserPointer(editState.rayCollideInfo.results->wco, entryCollObjectMsgHandler, NULL))
		{
			// get model/material/texture data and open context menu
			static UIMenuItem **items = NULL;
			GroupTracker *tracker = trackerFromTrackerHandle(SAFE_MEMBER(editState.rayCollideInfo.entry, tracker_handle));
			UIMenu *menu = NULL;

			eaDestroyEx(&items, ui_MenuItemFree);
			if (tracker)
			{
				EditorObject **objs = NULL;
				eaPush(&objs, editorObjectCreate(trackerHandleCreate(tracker), tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER));
				menu = edObjMenuPopupAtCursor(wleMenuRightClick, objs, hideDisabled);
				eaDestroy(&objs);
			}
			 
			if (!menu)
				menu = edObjMenuPopupAtCursor(wleMenuRightClick, NULL, hideDisabled);

			if (!!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowContextMenu", 1))
			{
				const Material *material = NULL;
				BasicTexture **textures = NULL;
				int i;

				if (editState.rayCollideInfo.model)
				{
					if (eaSize(&menu->items) > 0)
						eaPush(&items, ui_MenuItemCreate("[Menu Separator]", UIMenuSeparator, NULL, NULL, NULL));
					wleTrackerContextMenuCreateForModel(editState.rayCollideInfo.model, &items);
					for (i = 0; i < eaSize(&items); i++)
						ui_MenuAppendItem(menu, items[i]);
					eaClear(&items);
				}
				if (editState.rayCollideInfo.mat)
					material = editState.rayCollideInfo.mat;

				// if the ray hit a world collision entry...
				if (editState.rayCollideInfo.entry && editState.rayCollideInfo.model)
				{
					// search world cell octree to find drawable entry with same matrix/bounds as collision entry
					WorldDrawableEntry *drawable = wleCmdPopupMenuFindDrawable(editState.rayCollideInfo.entry);

					// if drawable entry found, find list of textures and create menu items for each one
					if (drawable)
					{
						WorldDrawableList *drawableList = drawable->draw_list;
						if (drawableList->lod_count > 0)
						{
							WorldDrawableLod *lod = &drawableList->drawable_lods[0];
							ModelLOD *modelLod = modelLoadLOD(editState.rayCollideInfo.model, 0);
							ModelLODData *modelLodData = modelLod ? modelLod->data : NULL;

							// get subobject index from ray collide results tri index
							if (modelLodData)
							{
								int triCount = 0;
								int texIDIdx = 0;
								while (texIDIdx < modelLodData->tex_count)
								{
									triCount += modelLodData->tex_idx[texIDIdx].count;
									if (triCount > editState.rayCollideInfo.results->tri.index)
										break;
									texIDIdx++;
								}
								if (texIDIdx < modelLodData->tex_count && (eaSize(&lod->subobjects) > texIDIdx) && eaSize(&lod->subobjects[texIDIdx]->material_draws))
								{
									int material_draw_idx;
									for (material_draw_idx=0; material_draw_idx<eaSize(&lod->subobjects[texIDIdx]->material_draws); material_draw_idx++)
									{
										MaterialDraw *matDraw = lod->subobjects[texIDIdx]->material_draws[material_draw_idx];
										if (!matDraw)
											continue;
										if (matDraw->textures)
										{
											for (i = 0; i < matDraw->tex_count; i++)
												eaPush(&textures, matDraw->textures[i]);
										}
										if (matDraw->material)
										{
											if (!matDraw->textures)
											{
												StashTable texNames = stashTableCreateWithStringKeys(16, StashDefault);
												StashTableIterator iter;
												StashElement el;

												materialGetTextureNames(matDraw->material, texNames, NULL);
												stashGetIterator(texNames, &iter);
												while (stashGetNextElement(&iter, &el))
												{
													char *texName = stashElementGetStringKey(el);
													BasicTexture *texture = texName ? texFind(texName, 1) : NULL;
													if (texture)
														eaPushUnique(&textures, texture);
												}

												stashTableDestroy(texNames);
											}
											material = matDraw->material;
										}
									}
								}
							}
						}
					}

					// add menu items
					if (material)
					{
						UIMenu *materialSubmenu = ui_MenuCreate("Material Submenu");
						char submenuText[1024];

						// create material submenu
						wleTrackerContextMenuCreateForMaterial(material, &items);
						eaQSort(textures, wleCmdPopupMenuTextureCmp);
						for (i = 0; i < eaSize(&textures); i++)
						{
							if (!i && material)
								eaPush(&items, ui_MenuItemCreate("[Menu Separator]", UIMenuSeparator, NULL, NULL, NULL));
							wleTrackerContextMenuCreateForTexture(textures[i], &items);
						}
						for (i = 0; i < eaSize(&items); i++)
							ui_MenuAppendItem(materialSubmenu, items[i]);

						// add submenu to context menu
						sprintf(submenuText, "Material \"%s\"", material->material_name);
						if (editState.rayCollideInfo.model)
							ui_MenuAppendItem(menu, ui_MenuItemCreate("[Menu Separator]", UIMenuSeparator, NULL, NULL, NULL));
						ui_MenuAppendItem(menu, ui_MenuItemCreate(submenuText, UIMenuSubmenu, NULL, NULL, (void*) materialSubmenu));
					}
				}

				eaDestroy(&textures);
				eaDestroy(&items);
			}
		}
		else
			edObjMenuPopupAtCursor(wleMenuRightClick, NULL, hideDisabled);
	}
	eaDestroyEx(&selection, editorObjectDeref);
}

// Copy selection text to windows clipboard
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.CopySelectionNames");
void wleCmdCopySelectionNames(void)
{
	EditorObject **selection = NULL;
	char *text = NULL;
	int i;

	edObjSelectionGetAll(&selection);
	estrCreate(&text);
	for (i = 0; i < eaSize(&selection); i++)
		estrConcatf(&text, "%s%s", i ? "\n" : "", wleUIEdObjGetDisplayText(selection[i]));
	winCopyUTF8ToClipboard(text);
	estrDestroy(&text);
	eaDestroy(&selection);
}

// Focus camera on selection
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.FocusCamera");
void wleCmdFocusCamera(void)
{
	GfxCameraController *camera = gfxGetActiveCameraController();
	Mat4 mat;

	if (edObjSelectionGetCount(EDTYPE_NONE) > 0)
	{
		//int i;
		F32 max_dist = 40.f;
		EditorObject **selectionList = NULL;

		edObjHarnessGetGizmoMatrix(mat);

		edObjSelectionGetAll(&selectionList);
		wleUIFocusCameraOnEdObjs(selectionList);

		// center tracker tree on selection
		if (eaSize(&selectionList) == 1)
			wleUITrackerTreeCenterOnEdObj(selectionList[0]);

		eaDestroy(&selectionList);
	}
	else
		globCmdParse("Camera.center");
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.FocusObject");
void wleCmdFocusObject(void)
{
	EditorObject **selection = NULL;
	EditorObject **target = NULL;

	// get center matrix
	edObjSelectionGetAll(&selection);
	if (!selection || eaSize(&selection) == 0)
		return;
	editState.focusedIdx = (editState.focusedIdx + 1) % eaSize(&selection);
	eaPush(&target, selection[editState.focusedIdx]);

	// move camera
	wleUIFocusCameraOnEdObjs(target);

	// focus on tracker tree
	wleAESetActiveQueued(target[0]);
	wleUITrackerTreeCenterOnEdObj(target[0]);

	eaDestroy(&target);
	eaDestroy(&selection);
}

bool wleCheckCmdDownTree(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Travel down tracker tree
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.DownTree");
void wleCmdDownTree(void)
{
	if (wleCheckCmdDownTree(NULL))
		edObjSelectDownTree();
}

bool wleCheckCmdUpTree(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Travel up tracker tree
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.UpTree");
void wleCmdUpTree(void)
{
	if (wleCheckCmdUpTree(NULL))
		edObjSelectUpTree();
}

bool wleCheckCmdDeselect(UserData unused)
{
	return (worldEditor.inited && !wleGizmoIsActive());
}

// Deselect all
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Deselect");
void wleCmdDeselect(void)
{
	if (wleCheckCmdDeselect(NULL))
	{
		TranslateGizmo *gizmo = edObjHarnessGetTransGizmo();
		if (TranslateGizmoIsSnapAlongAxes(gizmo) && TranslateGizmoGetSpecSnap(gizmo) != EditSnapNone && TranslateGizmoGetSpecSnap(gizmo) != EditSnapGrid)
			TranslateGizmoDisableAxes(gizmo);
		else if (editState.mode == EditSelectParent || editState.mode == EditPlaceObjects)
			wleChangeMode(EditNormal);
		else if (editState.lockedSelection)
			wleUISelectionLockWarn();
		else
			wleTrackerDeselectAll();
	}
}

bool wleCheckCmdInvertSelection(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Invert selection
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Invert");
void wleCmdInvertSelection(void)
{
	if (wleCheckCmdInvertSelection(NULL))
		wleSelectionInvert();
}

// Select children
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.SelectChildren");
void wleCmdSelectChildren(void)
{
	if (wleCheckDefault(NULL))
	{
		EditorObject **selection = NULL;
		EditorObject **children = NULL;
		int i;

		edObjSelectionGetAll(&selection);
		eaForEach(&selection, editorObjectRef);

		EditUndoBeginGroup(edObjGetUndoStack());
		edObjSelectionClear(EDTYPE_NONE);
		for (i = 0; i < eaSize(&selection); i++)
		{
			if (selection[i]->type && selection[i]->type->childFunc)
				selection[i]->type->childFunc(selection[i], &children);
		}
		edObjSelectList(children, true, true);
		EditUndoEndGroup(edObjGetUndoStack());

		eaDestroyEx(&selection, editorObjectDeref);
	}
}

bool wleCheckCmdLockSelection(UserData unused)
{
	return true;
}

// Toggle selection lock
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Lock");
void wleCmdLockSelection(void)
{
	if (wleCheckCmdLockSelection(NULL))
		editState.lockedSelection = !editState.lockedSelection;
	wleToolbarSelectionLockRefresh();
}

bool wleCheckCmdHideSelection(UserData unused)
{
	return (wleCheckDefault(NULL) && wleSelectionGetCount() > 0);
}

// Hide/unhide selection
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Hide");
void wleCmdHideSelection(void)
{
	if (wleCheckCmdHideSelection(NULL))
	{
		TrackerHandle **selected = NULL;
		int i;

		wleSelectionGetTrackerHandles(&selected);
		for (i = 0; i < eaSize(&selected); i++)
			wleTrackerToggleHide(selected[i]);
		eaDestroy(&selected);
	}
}

bool wleCheckCmdUnhideAll(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Unhide all
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Unhide");
void wleCmdUnhideAll(void)
{
	if (wleCheckCmdUnhideAll(NULL))
		wleTrackerUnhideAll();
}

bool wleCheckCmdFreezeSelection(UserData unused)
{
	return (wleCheckDefault(NULL) && edObjSelectionGetCount(EDTYPE_TRACKER));
}

// Freeze selection
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Freeze");
void wleCmdFreezeSelection(void)
{
	if (wleCheckCmdFreezeSelection(NULL))
	{
		TrackerHandle **selected = NULL;
		int i;

		wleSelectionGetTrackerHandles(&selected);
		for (i = 0; i < eaSize(&selected); i++)
			wleTrackerFreeze(selected[i]);
		eaDestroy(&selected);
	}
}

bool wleCheckCmdUnfreeze(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Unfreeze selection
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Unfreeze");
void wleCmdUnfreeze(void)
{
	if (wleCheckCmdUnfreeze(NULL))
		wleTrackerUnfreezeAll();
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.SubObjectEdit");
void wleCmdSubObjectEdit(void)
{
	//if (wleCheckCmdFreezeSelection(NULL)) // TomY TODO
	{
		TrackerHandle **selected = NULL;
		int i;

		wleSelectionGetTrackerHandles(&selected);
		for (i = 0; i < eaSize(&selected); i++)
			wleTrackerToggleEditSubObject(selected[i]);
		eaDestroy(&selected);
	}
}

bool wleCheckCmdCycleGizmo(UserData unused)
{
	return !wleGizmoIsActive();
}

// Cycle through gizmos
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.TransRot");
void wleCmdCycleGizmo()
{
	if (wleCheckCmdCycleGizmo(NULL))
		wleTransformModeCycle(editState.transformMode, editState.trackerType, inpLevelPeek(INP_SHIFT));
}

// Rotate gizmo/translate gizmo 
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.TransRotWhileDown");
void wleCmdTransRotWhileDown(int down)
{
	if (down)
		editState.queuedTransformMode = EditRotateGizmo;
	else
		editState.queuedTransformMode = EditTranslateGizmo;
}

bool wleCheckCmdSnapNormal(UserData unused)
{
	return (!wleGizmoIsActive() && editState.transformMode == EditTranslateGizmo);
}

// Toggle snap to normal
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ToggleNormalSnap");
void wleCmdSnapNormal(void)
{
	if (wleCheckCmdSnapNormal(NULL))
	{
		TranslateGizmo *transGizmo = edObjHarnessGetTransGizmo();
		TranslateGizmoSetSnapNormal(transGizmo, !TranslateGizmoGetSnapNormal(transGizmo));
	}
}

bool wleCheckCmdSnapClamp(UserData unused)
{
	return (!wleGizmoIsActive() && editState.transformMode == EditTranslateGizmo);
}

// Toggle snap clamping
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ToggleSnapClamp");
void wleCmdSnapClamp(void)
{
	if (wleCheckCmdSnapClamp(NULL))
	{
		TranslateGizmo *transGizmo = edObjHarnessGetTransGizmo();
		TranslateGizmoSetSnapClamp(transGizmo, !TranslateGizmoGetSnapClamp(transGizmo));
	}
}

bool wleCheckCmdCycleTransSnap(UserData unused)
{
	return (!wleGizmoIsActive() && TranslateGizmoGetSpecSnap(edObjHarnessGetTransGizmo()) != EditSnapNone);
}

// Cycle translation snap modes
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.CycleTransSnap");
void wleCmdCycleTransSnap(void)
{
	if (wleCheckCmdCycleTransSnap(NULL))
	{
		TranslateGizmo *gizmo = edObjHarnessGetTransGizmo();
		EditSpecialSnapMode mode = TranslateGizmoGetSpecSnap(gizmo);
		if (mode != EditSnapNone)
			wleSetTranslateSnapMode((mode + 1) % EditSnapNone);
	}
}

bool wleCheckCmdSetTransSnap(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Set translation snap mode
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.SetTransSnap");
void wleCmdSetTransSnap(const char *mode)
{
	EditSpecialSnapMode modeEnum = StaticDefineIntGetInt(EditSpecialSnapModeEnum, mode);

	if (modeEnum == -1)
		modeEnum = EditSnapSmart;
	if (wleCheckCmdSetTransSnap(NULL))
		wleSetTranslateSnapMode(modeEnum);
}

AUTO_CMD_INT(editState.inputData.transSnapEnabledKey, WorldEditorEnableTransSnap) ACMD_CALLBACK(wleCmdEnableTransSnap);
void wleCmdEnableTransSnap(CMDARGS)
{
	TranslateGizmo *transGizmo = edObjHarnessGetTransGizmo();

	if (!editState.inputData.transSnapEnabledKey)
		wleSetTranslateSnapMode(EditSnapNone);
	else
		wleSetTranslateSnapMode(TranslateGizmoGetLastSnap(transGizmo));
}

// Reduce translation snap resolution
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.DecTransSnap");
void wleCmdDecTransSnap(void)
{
	TranslateGizmoSetSnapResolution(edObjHarnessGetTransGizmo(), TranslateGizmoGetSnapResolution(edObjHarnessGetTransGizmo()) - 1);
	wleGizmosUITransGizmoChanged(edObjHarnessGetTransGizmo());
}

// Increase translation snap resolution
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.IncTransSnap");
void wleCmdIncTransSnap(void)
{
	TranslateGizmoSetSnapResolution(edObjHarnessGetTransGizmo(), TranslateGizmoGetSnapResolution(edObjHarnessGetTransGizmo()) + 1);
	wleGizmosUITransGizmoChanged(edObjHarnessGetTransGizmo());
}

bool wleCheckCmdCycleTransAxes(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Cycle translation axes
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.CycleTransAxes");
void wleCmdCycleTransAxes(void)
{
	if (wleCheckCmdCycleTransAxes(NULL))
		TranslateGizmoCycleAxes(edObjHarnessGetTransGizmo());
}

bool wleCheckCmdToggleTransX(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Toggle x-axis translation
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ToggleTransX");
void wleCmdToggleTransX(void)
{
	if (wleCheckCmdToggleTransX(NULL))
		TranslateGizmoToggleAxes(edObjHarnessGetTransGizmo(), true, false, false);
}

bool wleCheckCmdToggleTransY(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Toggle y-axis translation
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ToggleTransY");
void wleCmdToggleTransY(void)
{
	if (wleCheckCmdToggleTransY(NULL))
		TranslateGizmoToggleAxes(edObjHarnessGetTransGizmo(), false, true, false);
}

bool wleCheckCmdToggleTransZ(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Toggle z-axis translation
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ToggleTransZ");
void wleCmdToggleTransZ(void)
{
	if (wleCheckCmdToggleTransZ(NULL))
		TranslateGizmoToggleAxes(edObjHarnessGetTransGizmo(), false, false, true);
}

bool wleCheckCmdToggleRotSnap(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Toggle rotation snap enabled
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ToggleRotSnap");
void wleCmdToggleRotSnap(void)
{
	if (wleCheckCmdToggleRotSnap(NULL))
	{
		RotateGizmo *rotGizmo = edObjHarnessGetRotGizmo();
		RotateGizmoEnableSnap(rotGizmo, !RotateGizmoIsSnapEnabled(rotGizmo));
		wleGizmosUIRotGizmoChanged(edObjHarnessGetRotGizmo());
	}
}

// Reduce rotation snap resolution
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.DecRotSnap");
void wleCmdDecRotSnap(void)
{
	RotateGizmoSetSnapResolution(edObjHarnessGetRotGizmo(), RotateGizmoGetSnapResolution(edObjHarnessGetRotGizmo()) - 1);
	wleGizmosUIRotGizmoChanged(edObjHarnessGetRotGizmo());
}

// Increase rotation snap resolution
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.IncRotSnap");
void wleCmdIncRotSnap(void)
{
	RotateGizmoSetSnapResolution(edObjHarnessGetRotGizmo(), RotateGizmoGetSnapResolution(edObjHarnessGetRotGizmo()) + 1);
	wleGizmosUIRotGizmoChanged(edObjHarnessGetRotGizmo());
}

bool wleCheckCmdWorldPivot(UserData unused)
{
	return (!wleGizmoIsActive());
}

// World alignment toggle
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.WorldPivot");
void wleCmdWorldPivot(void)
{
	if (wleCheckCmdWorldPivot(NULL))
	{
		TranslateGizmoSetAlignedToWorld(edObjHarnessGetTransGizmo(), !TranslateGizmoGetAlignedToWorld(edObjHarnessGetTransGizmo()));
		wleGizmosUITransGizmoChanged(edObjHarnessGetTransGizmo());
	}
}

bool wleCheckCmdResetRot(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Reset the object's rotation
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ResetRot");
void wleCmdResetRot(void)
{
	if (wleCheckCmdResetRot(NULL))
	{
		RotateGizmo *rotGizmo = edObjHarnessGetRotGizmo();
		Mat4 resetMat;

		RotateGizmoGetMatrix(rotGizmo, resetMat);
		copyMat3(unitmat, resetMat);
		edObjHarnessSetGizmoMatrixAndCallback(resetMat);
	}
}

bool wleCheckCmdLockFiles(UserData unused)
{
	return (!wleGizmoIsActive() && edObjSelectionGetCount(EDTYPE_TRACKER) > 0);
}

// Lock files belonging to selection
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.LockFiles");
void wleCmdLockFiles(void)
{
	const char **fileNames = NULL;
	if (wleCheckCmdLockFiles(NULL))
	{
		int i;
		for (i = 0; i < edObjSelectionGetCount(EDTYPE_TRACKER); i++)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(edObjSelectionGet(EDTYPE_TRACKER)[i]->obj);
			assert(tracker);
			if (!groupIsObjLib(tracker->def))
				continue;

			eaPush(&fileNames, tracker->def->filename);
		}
	}
	wleOpLockFiles(fileNames, NULL, NULL);
	eaDestroy(&fileNames);
}

bool wleCheckCmdFindAndReplace(UserData unused)
{
	return wleCheckDefault(NULL);
}

// Lock files belonging to selection
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.FindAndReplace");
void wleCmdFindAndReplace(void)
{
	wleUIFindAndReplaceDialogCreate();
}

bool wleCheckCmdInstantiate(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Create new instance
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Instance");
void wleCmdInstantiate(void)
{
	if (wleCheckCmdInstantiate(NULL))
	{
		EditUndoStack *stack = edObjGetUndoStack();
		EditorObject **list = edObjSelectionGet(EDTYPE_TRACKER);
		EditUndoBeginGroup(stack);
		FOR_EACH_IN_EARRAY(list, EditorObject, object)
		{
			TrackerHandle *handle = object->obj;
			if (handle)
				wleOpInstance(handle);
		}
		FOR_EACH_END;
		EditUndoEndGroup(stack);
	}
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.InstanceForUGC");
void wleCmdInstantiatePrefix(void)
{
	if (wleCheckCmdInstantiate(NULL))
	{
		EditUndoStack *stack = edObjGetUndoStack();
		EditorObject **list = edObjSelectionGet(EDTYPE_TRACKER);
		EditUndoBeginGroup(stack);
		FOR_EACH_IN_EARRAY(list, EditorObject, object)
		{
			TrackerHandle *handle = object->obj;
			if (handle)
			{
				char new_name[256];
				GroupTracker *tracker = trackerFromTrackerHandle(handle);
				sprintf(new_name, "UGC_%s", tracker->def->name_str);
				wleOpInstance(handle);
				wleOpRename(handle, new_name);
			}
		}
		FOR_EACH_END;
		EditUndoEndGroup(stack);
	}
}

bool wleCheckCmdTouch(UserData unused)
{
	return (wleCheckDefault(NULL) && edObjSelectionGetCount(EDTYPE_TRACKER) > 0);
}

// Increment mod time of the selected group defs
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Touch");
void wleCmdTouch(void)
{
	if (wleCheckCmdTouch(NULL))
	{
		EditorObject **selection = edObjSelectionGet(EDTYPE_TRACKER);
		TrackerHandle **touchObjs = NULL;
		int i;
		for (i = 0; i < eaSize(&selection); i++)
		{
			if (selection[i]->obj)
			{
				eaPush(&touchObjs, selection[i]->obj);
			}
		}
		wleOpTouch(touchObjs);
		eaDestroy(&touchObjs);
	}
}

bool wleCheckCmdDuplicate(UserData unused)
{
	return (wleCheckDefault(NULL) && edObjSelectionGetCount(EDTYPE_NONE) > 0);
}

typedef struct ParentChildTracking
{
	GroupTracker *parent;
	TrackerHandle **handles;
} ParentChildTracking;

void trackerActionDuplicate(EditorObject **selection)
{
	if (wleCheckCmdDuplicate(NULL))
	{
		ParentChildTracking **eaTracking = NULL;
		TrackerHandle *parentCheck;
		GroupTracker *parent;
		EditorObject **eaNewlyCreated = NULL;
		int i, j;

		// find the common parent of the selection and copy into it
		for (i = 0; i < eaSize(&selection); i++)
		{
			parentCheck = trackerHandleCopy(selection[i]->obj);
			trackerHandlePopUID(parentCheck);
			parent = trackerFromTrackerHandle(parentCheck);

			for (j = 0; j < eaSize(&eaTracking); j++)
			{
				if (eaTracking[j]->parent == parent)
				{
					eaPush(&eaTracking[j]->handles, selection[i]->obj);
					break;
				}
			}

			if (j == eaSize(&eaTracking))
			{
				ParentChildTracking *tracking = calloc(1, sizeof(ParentChildTracking));
				tracking->parent = parent;
				eaPush(&tracking->handles, selection[i]->obj);
				eaPush(&eaTracking, tracking);
			}
		}
		
		for (i = 0; i < eaSize(&eaTracking); i++)
		{
			wleOpCopyEx(trackerHandleFromTracker(eaTracking[i]->parent), eaTracking[i]->handles, (i == (eaSize(&eaTracking) - 1)));
			edObjSelectionGetAll(&eaNewlyCreated);
			eaDestroy(&eaTracking[i]->handles);
			free(eaTracking[i]);
		}
		edObjSelectList(eaNewlyCreated, false, true);
		eaDestroy(&eaTracking);
	}
}

void trackerActionToggleSubobjects(EditorObject **selection)
{
	int i;
	for (i = 0; i < eaSize(&selection); i++)
		wleTrackerToggleEditSubObject(selection[i]->obj);
}

bool wleCheckCmdRename(UserData unused)
{
	return (wleCheckDefault(NULL) && edObjSelectionGetCount(EDTYPE_NONE) == 1);
}

bool wleCheckCmdSetDefaultParent(UserData unused)
{
	return wleCheckDefault(NULL) && edObjSelectionGetCount(EDTYPE_TRACKER) == 1;
}

bool wleCheckCmdMoveToDefaultParent(UserData unused)
{
	return (wleCheckDefault(NULL) && (wleSelectionGetCount() + edObjSelectionGetCount(EDTYPE_LOGICAL_GROUP)) == edObjSelectionGetCount(EDTYPE_NONE));
}

// Move selected objects to default parent
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.MoveToDefaultParent");
void wleCmdMoveToDefaultParent(void)
{
	if(wleCheckCmdMoveToDefaultParent(NULL))
	{
		TrackerHandle *parent = trackerHandleCopy(editState.defaultParent);

		TrackerHandle **handles = NULL;
		wleSelectionGetTrackerHandles(&handles);

		wleOpAddToGroup(parent, handles, -1);

		eaDestroy(&handles);
		trackerHandleDestroy(parent);
	}
	else
		emStatusPrintf("Only trackers and logical groups can be reparented; please deselect everything else before proceeding.");
}

// Copy selected objects to the scratch layer
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.CopyToScratch");
void wleCmdCopyToScratch(void)
{
	ZoneMapLayer *layer = wleGetScratchLayer(true);
	TrackerHandle *handle = trackerHandleCreate(layerGetTracker(layer));

	TrackerHandle **handles = NULL;
	wleSelectionGetTrackerHandles(&handles);

	wleOpCopy(handle, handles);

	eaDestroy(&handles);
	trackerHandleDestroy(handle);
}

bool wleCheckCmdGroupExpandAll(UserData unused)
{
	return wleCheckDefault(NULL);
}

// Create new instance
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Rename");
void wleCmdRename(void)
{
	if (wleCheckCmdRename(NULL))
	{
		EditorObject **selection = NULL;
		edObjSelectionGetAll(&selection);
		wleUIEdObjRenameDialog(selection[0]);
	}
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.SetDefaultParent");
void wleCmdSetDefaultParent(void)
{
	if (wleCheckCmdSetDefaultParent(NULL))
	{
		EditorObject **selection = edObjSelectionGet(EDTYPE_TRACKER);
		EditorObject *edObj = selection[0];
		TrackerHandle *handle = edObj->obj;

		if (handle) wleSetDefaultParent(handle);
	}
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.GroupExpandAll");
void wleCmdGroupExpandAll(void)
{
	if (wleCheckCmdGroupExpandAll(NULL))
	{
		int i;
		EditorObject **selection = NULL;
		edObjSelectionGetAll(&selection);
		eaForEach(&selection, editorObjectRef);

		for(i = 0; i < eaSize(&selection); i++)
		{
			EditorObject *edObj = selection[i];
			UITreeNode *node = wleUITrackerTreeGetNodeForEdObj(edObj);
			if(node)
				ui_TreeNodeExpandEx(node, true);
		}

		eaDestroyEx(&selection, editorObjectDeref);
	}
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ReseedAll");
void wleCmdReseedAll(void)
{
	if (wleCheckDefault(NULL))
	{
		TrackerHandle **handles = NULL;
		wleSelectionGetTrackerHandles(&handles);
		wleOpReseed(handles);
		eaDestroy(&handles);
	}
}

bool wleCheckCmdGroup(UserData unused)
{
	return (wleCheckDefault(NULL) && (edObjSelectionGetCount(EDTYPE_TRACKER) + edObjSelectionGetCount(EDTYPE_LOGICAL_GROUP)) > 0);
}

// Group
typedef struct WleCmdLogicalGroupParams
{
	TrackerHandle *activeScopeHandle;
	const char **childNames;
} WleCmdLogicalGroupParams;

static void wleCmdLogicalGroupCancelCallback(WleCmdLogicalGroupParams *params)
{
	trackerHandleDestroy(params->activeScopeHandle);
	eaDestroy(&params->childNames);
	free(params);
}

static bool wleCmdLogicalGroupOkCallback(const char *uniqueName, WleCmdLogicalGroupParams *params)
{
	if (wleOpLogicalGroup(params->activeScopeHandle, uniqueName, params->childNames, false))
	{
		wleCmdLogicalGroupCancelCallback(params);
		return true;
	}
	return false;
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Group");
void wleCmdGroup(void)
{
	if (wleCheckCmdGroup(NULL))
	{
		if (editorUIState->showingLogicalTree)
		{
			WorldScope *zmapScope = (WorldScope*) zmapGetScope(NULL);
			GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
			WorldScope *closestScope = scopeTracker ? scopeTracker->closest_scope : zmapScope;
			const char **uniqueNames = NULL;
			EditorObject **selection = NULL;
			int i;

			edObjSelectionGetAll(&selection);
			for (i = 0; i < eaSize(&selection); i++)
			{
				const char *uniqueName = NULL;
				if (selection[i]->type->objType == EDTYPE_TRACKER)
				{
					GroupTracker *tracker = trackerFromTrackerHandle(selection[i]->obj);
					if (tracker)
						uniqueName = trackerGetUniqueScopeName(scopeTracker ? scopeTracker->def : NULL, tracker, NULL);
				}
				else if (selection[i]->type->objType == EDTYPE_LOGICAL_GROUP)
				{
					WorldLogicalGroup *group;
					char *name;

					if (stashFindPointer(zmapScope->name_to_obj, selection[i]->obj, &group) && stashFindPointer(closestScope->obj_to_name, group, &name))
						uniqueName = name;
				}
				if (uniqueName)
					eaPush(&uniqueNames, uniqueName);
			}
			if (eaSize(&uniqueNames) > 0 && wleOpLogicalGroup(editorUIState->trackerTreeUI.activeScopeTracker, NULL, uniqueNames, true))
			{
				WleCmdLogicalGroupParams *params = calloc(1, sizeof(*params));
				params->activeScopeHandle = trackerHandleCopy(editorUIState->trackerTreeUI.activeScopeTracker);
				params->childNames = uniqueNames;
				wleUIUniqueNameRequiredDialog("Enter the unique name of this logical group.", NULL, wleCmdLogicalGroupOkCallback, params, wleCmdLogicalGroupCancelCallback, params);
			}
			else
				eaDestroy(&uniqueNames);
			eaDestroy(&selection);
		}
		else
		{
			TrackerHandle **handles = NULL;
			Mat4 newPivot;

			wleSelectionGetTrackerHandles(&handles);
			edObjHarnessGetGizmoMatrix(newPivot);
			wleOpGroup(handles, newPivot);
			eaDestroy(&handles);
		}
	}
}

bool wleCheckCmdAddToGroup(UserData unused)
{
	return (wleCheckDefault(NULL) && wleSelectionGetCount() > 0);
}

// Add selected objects to an existing group
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.AddToGroup");
void wleCmdAddToGroup(void)
{
	if (wleCheckCmdAddToGroup(NULL))
	{
		if (wleSelectionGetCount() + edObjSelectionGetCount(EDTYPE_LOGICAL_GROUP) != edObjSelectionGetCount(EDTYPE_NONE))
			emStatusPrintf("Only trackers and logical groups can be reparented; please deselect everything else before proceeding.");
		else
			editState.mode = EditSelectParent;
	}
}

bool wleCheckCmdUngroup(UserData unused)
{
	return (wleCheckDefault(NULL) && (edObjSelectionGetCount(EDTYPE_TRACKER) + edObjSelectionGetCount(EDTYPE_LOGICAL_GROUP)) == 1);
}

// Ungroup selection
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Ungroup");
void wleCmdUngroup(void)
{
	if (wleCheckCmdUngroup(NULL))
	{
		if (editorUIState->showingLogicalTree)
		{
			EditorObject **objs = edObjSelectionGet(EDTYPE_LOGICAL_GROUP);
			WorldScope *zmapScope = (WorldScope*) zmapGetScope(NULL);
			GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
			WorldScope *closestScope = scopeTracker ? scopeTracker->closest_scope : zmapScope;
			WorldLogicalGroup *group;
			char *scopeName;

			if (zmapScope && stashFindPointer(zmapScope->name_to_obj, objs[0]->obj, &group) && stashFindPointer(closestScope->obj_to_name, group, &scopeName))
				wleOpLogicalUngroup(editorUIState->trackerTreeUI.activeScopeTracker, scopeName);
		}
		else
		{
			TrackerHandle* selected = edObjSelectionGet(EDTYPE_TRACKER)[0]->obj;
			assert(selected);
			wleOpUngroup(selected);
		}
	}
}

bool wleCheckCmdDelete(UserData unused)
{
	return (wleCheckDefault(NULL) && edObjSelectionGetCount(EDTYPE_NONE) > 0);
}
void trackerActionDelete(EditorObject **selection)
{
	if (wleCheckCmdDelete(NULL))
	{
		TrackerHandle **handles = NULL;
		int i;

		for (i = 0; i < eaSize(&selection); i++)
			eaPush(&handles, selection[i]->obj);
		if (eaSize(&handles) == 1)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(handles[0]);
			if (tracker && !tracker->parent)
			{
				ZoneMapLayer *layer = zmapGetLayerByName(NULL, tracker->def->filename);
				if (layer)
					wleUIDeleteLayerDialogCreate(layer);
				return;
			}
		}
		wleOpDelete(handles);
		eaDestroy(&handles);
	}	
}

bool wleCheckCmdEditOrig(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Toggle edit instances
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.EditOrig");
void wleCmdEditOrig(void)
{
	if (wleCheckCmdEditOrig(NULL))
	{
		editState.editOriginal = !editState.editOriginal;
		EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "EditOriginal", editState.editOriginal);
	}
	wleToolbarEditOrigRefresh();
}

bool wleCheckCmdSetScene(UserData unused)
{
	return (wleCheckDefault(NULL));
}

bool wleCheckCmdNewLayer(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// New layer
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.NewLayer");
void wleCmdNewLayer(void)
{
	if (wleCheckCmdNewLayer(NULL))
		wleUINewLayerDialog();
}

bool wleCheckCmdImportLayer(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Import layer
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ImportLayer");
void wleCmdImportLayer(void)
{
	if (wleCheckCmdImportLayer(NULL))
		wleUIImportLayerDialogCreate();
}

bool wleCheckCmdNewZoneMap(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// New ZoneMap
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.NewGrid");
void wleCmdNewZoneMap(void)
{
	if (wleCheckCmdNewZoneMap(NULL))
	{
		if (zmapOrLayersUnsaved(NULL) && ui_ModalDialog("Confirm Reverting Changes",
			"You are about to open a new map and you have unsaved changes.\n"
			"Are you sure you want to lose your changes?",
			ColorBlack, UIYes | UINo) == UINo)
			return;
		wleUINewZoneMapDialogCreate();
	}
}

bool wleCheckCmdOpenZoneMap(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Open ZoneMap
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.OpenGrid");
void wleCmdOpenZoneMap(void)
{
	if (wleCheckCmdOpenZoneMap(NULL)) {
		if (zmapOrLayersUnsaved(NULL) && ui_ModalDialog("Confirm Reverting Changes",
			"You are about to open a new map and you have unsaved changes.\n"
			"Are you sure you want to lose your changes?",
			ColorBlack, UIYes | UINo) == UINo)
			return;
		wleUIOpenZoneMapDialogCreate();
	}
}

bool wleCheckCmdReloadFromSource(UserData unused)
{
	return (wleCheckDefault(NULL));
}

// Reload from source files
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ReloadFromSource");
void wleCmdReloadFromSource(void)
{
	if (wleCheckCmdReloadFromSource(NULL)) {

		if (ui_ModalDialog( "Confirm Reload From Source",
							"This will delete all your bins and make new ones.  "
							"Making new bins is a safe but potentially very long process."
							"\n"
							"Are you absolutely sure you want to reload from source?",
							ColorBlack, UIYes | UINo) == UINo)
		{
			return;
		}

		wleOpReloadFromSource();
	}
}

// Initiate a UGC publish
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("ugc_publish");
void wleCmdUGCPublish(void)
{
	if (wleCheckCmdReloadFromSource(NULL))
		wleOpUGCPublish();
}

bool wleCheckCmdSave(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Save
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.Save");
void wleCmdSave(void)
{
	if (wleCheckCmdSave(NULL))
		wleOpSave();
}

bool wleCheckCmdSaveAs(UserData unused)
{
	return (!wleGizmoIsActive());
}

// Save ZoneMap As
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.SaveAs");
void wleCmdSaveAs(void)
{
	if (wleCheckCmdSaveAs(NULL))
		wleUISaveZoneMapAsDialogCreate();
}

bool wleCheckCmdSaveToLib(UserData unused)
{
	return (wleCheckDefault(NULL));
}

bool wleCheckCmdCopyToScratch(UserData unused)
{
	int i;
	TrackerHandle **handles = NULL;
	ZoneMapLayer *layer;

	if (!wleIsScratchVisible())
	{
		return false;
	}

	wleSelectionGetTrackerHandles(&handles);
	if (eaSize(&handles) == 0)
	{
		return false;
	}

	layer = wleGetScratchLayer(false);

	if (!layer)
	{
		return true;
	}

	for (i = 0; i < eaSize(&handles); i++)
	{
		if (strstr(handles[i]->layer_name, layer->name))
		{
			return false;
		}
	}

	return true;
}

// Save to object library
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.SaveToLib");
void wleCmdSaveToLib(void)
{
	if (wleCheckCmdSaveToLib(NULL))
	{
		int i, j;
		TrackerHandle **handles = NULL;
		GroupTracker **trackers = NULL;
		GroupTracker *tracker;
		wleSelectionGetTrackerHandles(&handles);
		if (eaSize(&handles) < 1)
			return;
		tracker = trackerFromTrackerHandle(handles[0]);
		assert(tracker && tracker->def);
		eaPush(&trackers, tracker);
		for (i = 1; i < eaSize(&handles); i++)
		{
			bool found = false;
			tracker = trackerFromTrackerHandle(handles[i]);
			assert(tracker && tracker->def);
			for (j = 0; j < i; j++)
			{
				if (tracker->def == trackers[j]->def)
				{
					// Multiple instances of the same def get removed from the list
					found = true;
					break;
				}
			}
			if (!found)
				eaPush(&trackers, tracker);
		}
		if (eaSize(&trackers) == 1)
		{
			if (groupIsObjLib(trackers[0]->def))
				emStatusPrintf("Cannot resave object library piece \"%s\"! Please instance \"%s\" into the layer first.", trackers[0]->def->name_str, trackers[0]->def->name_str);
			else
				wleUISaveToLibDialogCreate(trackers);
		}
		else
		{
			for (i = 0; i < eaSize(&trackers); i++)
			{
				tracker = trackers[i];
				if (groupIsObjLib(tracker->def))
				{
					emStatusPrintf("Cannot resave object library piece \"%s\"! Please instance \"%s\" into the layer first.", tracker->def->name_str, tracker->def->name_str);
					return;
				}
				if (!resIsValidName(tracker->def->name_str))
				{
					emStatusPrintf("Invalid object name: %s.", tracker->def->name_str);
					return;
				}
			}
			wleUISaveToLibDialogCreate(trackers);
		}
		eaDestroy(&handles);
		eaDestroy(&trackers);
	}
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.ClearUndoStack");
void wleCmdClearUndoStack(void)
{
	EditUndoStackClear(edObjGetUndoStack());
}

// Placement shortcuts
bool wleCheckCmdPlaceObject(UserData unused)
{
	return wleCheckDefault(NULL);
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.PlaceObject");
void wleCmdPlaceObject(const char *pcObjectName)
{
	int uid = objectLibraryUIDFromObjName(pcObjectName);
	if (uid)
		wleObjectPlace(uid);
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.DeselectAndPlaceObject");
void wleCmdDeselectAndPlaceObject(const char *pcObjectName)
{
	wleCmdDeselect();
	wleCmdPlaceObject(pcObjectName);
}

/********************
* EDITOR OBJECT FRAMEWORK INTEGRATION
********************/
/******
* This struct is used to hold the marquee detection parameters - these are passed to the tree traversal
* helper.
******/
typedef struct WleMarqueeDetectParams
{
	WleFilter *filter;
	Vec3 marqueeMin, marqueeMax;
	Vec3 camDir;
	Mat4 cam;
	int depth;
	EditorObject ***edObjs;
	Mat44 scrProjMat;
	bool crossingMode;
} WleMarqueeDetectParams;

/******
* This function is used to test for axis-aligned box containment.  This is used in marquee selection.
* PARAMS:
*   outerMin - Vec3 "min" vector for potential outer box
*   outerMax - Vec3 "max" vector for potential outer box
*   innerMin - Vec3 "min" vector for potential inner box
*   innerMax - Vec3 "max" vector for potential inner box
******/
bool wleBoxContainsAA(Vec3 outerMin, Vec3 outerMax, Vec3 innerMin, Vec3 innerMax)
{
	int i;
	for (i = 0; i < 3; i++)
	{
		if (!(innerMin[i] >= outerMin[i] && innerMin[i] <= outerMax[i] && innerMax[i] >= outerMin[i] && innerMax[i] <= outerMax[i]))
			return false;
	}
	return true;
}

/******
* This function is the tree traversal callback for marquee selection.
* PARAMS:
*   marqueeParams - WleMarqueeDetectParams struct containing the marquee detection parameters
*   params - GroupTreeTraverserDrawParams containing info about the currently traversed tracker, populated by the traverser
*   matSwapsUnused - unused
*   swapsUnused - unused
* RETURNS:
*   int 0 if recursion should not continue, 1 if recursion should continue
******/
static int wleEdObjMarqueeDetectTraverseFunc(WleMarqueeDetectParams *marqueeParams, TrackerTreeTraverserDrawParams *params)
{
	GroupTracker *tracker = params->tracker;

	if (tracker && tracker->def)
	{
		// do not select invisible trackers or their children
		if (tracker->invisible)
			return 0;
		// recurse transparently through layer trackers
		else if (layerGetTracker(tracker->parent_layer) == tracker)
			return 1;
		else
		{
			TrackerHandle *handle = NULL;
			EditorObject *obj = NULL;
			bool filterResult = false;

			Vec3 bottomLeft, topRight;
			Vec4_aligned boxVerts[8];
			bool boxCollision;
			int i, j;

			if (tracker->def->property_structs.curve && !tracker->subObjectEditing)
			{
				for (j = 0; j < eafSize(&tracker->def->property_structs.curve->spline.spline_points); j += 3)
				{
					Vec3 min, max;
					//Vec3 bottomLeft, topRight;
					Mat4 world_mat;

					identityMat3(world_mat);
					mulVecMat4(&tracker->def->property_structs.curve->spline.spline_points[j], params->world_mat, world_mat[3]);

					setVec3(max, 3, 3, 3);
					setVec3(min, -3, -3, -3);

					// project the tracker's bounding box onto the screen and get the axis-aligned bounding box of the
					// projected vertices
					editLibFindScreenCoords(min, max, world_mat, marqueeParams->scrProjMat, bottomLeft, topRight);

					if ((marqueeParams->crossingMode && 
						boxBoxCollision(marqueeParams->marqueeMin, marqueeParams->marqueeMax, bottomLeft, topRight)) ||
						(!marqueeParams->crossingMode && 
						wleBoxContainsAA(marqueeParams->marqueeMin, marqueeParams->marqueeMax, bottomLeft, topRight)))
					{
						handle = trackerHandleCreate(tracker);
						eaPush(marqueeParams->edObjs, editorObjectCreate(handle, tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER));
						return 0;
					}
				}
			}

			// project the tracker's bounding box onto the screen and get the axis-aligned bounding box of the
			// projected vertices
			editLibFindScreenCoords(tracker->def->bounds.min, tracker->def->bounds.max, params->world_mat, marqueeParams->scrProjMat, bottomLeft, topRight);
			mulBounds(tracker->def->bounds.min, tracker->def->bounds.max, params->world_mat, boxVerts);

			// we also calculate distances from each bounding vertex to the plane that intersects the
			// camera whose normal is the camera direction - this allows us to determine the "z" bound
			// so that objects that are behind the camera or too far in front won't get selected
			for (i = 0; i < 8; i++)
			{
				Vec3 vertDir;
				float distFromCamPlane;
				subVec3(boxVerts[i], marqueeParams->cam[3], vertDir);
				distFromCamPlane = dotVec3(marqueeParams->camDir, vertDir);
				if (!i || bottomLeft[2] > distFromCamPlane)
					bottomLeft[2] = distFromCamPlane;
				if (!i || topRight[2] < distFromCamPlane)
					topRight[2] = distFromCamPlane;
			}

			// end recursion if the boxes don't collide at all
			boxCollision = boxBoxCollision(marqueeParams->marqueeMin, marqueeParams->marqueeMax, bottomLeft, topRight);
			if (!boxCollision)
				return 0;

			// do filter criteria check if filter is ignoring node state
			if (marqueeParams->filter && marqueeParams->filter->ignoreNodeState)
			{
				handle = trackerHandleCreate(tracker);
				obj = editorObjectCreate(handle, tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER);
				filterResult = wleFilterApply(obj, marqueeParams->filter);
			}

			// recurse to children if the tracker is open and the filter is respecting node states
			if (tracker->open &&
				(!marqueeParams->filter || !marqueeParams->filter->ignoreNodeState) && 
				(tracker->child_count > 0 ||
				(tracker->def && tracker->def->property_structs.patrol_properties && eaSize(&tracker->def->property_structs.patrol_properties->patrol_points) > 0) ||
				(tracker->def && tracker->def->property_structs.encounter_properties && eaSize(&tracker->def->property_structs.encounter_properties->eaActors) > 0)))
				return 1;

			// if the current tracker is bound by our marquee (in non-crossing mode) or touches our marquee (in crossing mode), then add it to the selection
			if ((marqueeParams->crossingMode && boxCollision) ||
				(!marqueeParams->crossingMode && wleBoxContainsAA(marqueeParams->marqueeMin, marqueeParams->marqueeMax, bottomLeft, topRight)))
			{
				// use existing filter application result (which means there is a filter ignoring the node states)
				if (obj)
				{
					if (filterResult)
					{
						eaPush(marqueeParams->edObjs, obj);
						return 0;
					}
					// when ignoring the node state, we continue to recurse
					else
					{
						editorObjectDeref(obj);
						return 1;
					}
				}
				else
				{
					handle = trackerHandleCreate(tracker);
					obj = editorObjectCreate(handle, tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER);

					if (wleFilterApply(obj, marqueeParams->filter))
						eaPush(marqueeParams->edObjs, obj);
					else
						editorObjectDeref(obj);
					return 0;
				}
			}
			else
			{
				if (obj)
					editorObjectDeref(obj);
				return 0;
			}
		}
	}
	else
		return 0;
}

/******
* This function is the callback to the EditorObject framework for handling marquee selection.  It is
* passed the marquee size, depth, and a screen projection matrix.  This function takes those marquee
* parameters and recurses down the tracker tree looking for trackers that fit into that marquee, adding
* those trackers to the selection.
* PARAMS:
*   mouseX - int x coordinate of top-left corner of marquee
*   mouseY - int y coordinate of top-left corner of marquee
*   mouseX2 - int x coordinate of bottom-right corner of marquee
*   mouseY2 - int y coordinate of bottom-right corner of marquee
*   depth - int depth of the marquee (i.e. maximum distance to selected objects)
*   cam - Mat4 camera world matrix
*   scrProjMat - Mat44 transformation matrix from world coordinates to the "canonical view volume"
*   edObjs - EditorObject EArrayHandle where the detected selection should be added
*   crossingMode - bool indicating whether to detect objects in crossing or non-crossing mode
******/
static void wleEdObjTrackerMarqueeDetect(int mouseX, int mouseY, int mouseX2, int mouseY2, int depth, Mat4 cam, Mat44 scrProjMat, EditorObject ***edObjs, bool crossingMode)
{
	WleMarqueeDetectParams params;
	int i;

	// cache the parameters for the tree traversal
	params.marqueeMin[0] = mouseX;
	params.marqueeMin[1] = mouseY;
	params.marqueeMin[2] = 0;
	params.marqueeMax[0] = mouseX2;
	params.marqueeMax[1] = mouseY2;
	params.marqueeMax[2] = depth;
	params.depth = depth;
	params.crossingMode = crossingMode;
	scaleVec3(cam[2], -1, params.camDir);
	copyMat4(cam, params.cam);
	copyMat44(scrProjMat, params.scrProjMat);
	params.edObjs = edObjs;
	params.filter = ui_ComboBoxGetSelectedObject(editorUIState->toolbarUI.marqueeFilterCombo);

	// traverse all group trees to find objects in the marquee
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer)
			trackerTreeTraverse(layer, layerGetTracker(layer), wleEdObjMarqueeDetectTraverseFunc, &params);
	}
}

/******
* Helper function to detect curve clicks.
*******/
bool wleEdObjCurveClickHelper(Vec3 start, Vec3 end, GroupTracker *tracker, EditorObject ***edObjList, Vec3 pos)
{
	int i;
	if (!tracker)
		return false;
	if (tracker->def && tracker->def->property_structs.curve &&
		!tracker->subObjectEditing)
	{
		F32 collide_offset;
		Vec3 collide_pos;
		Mat4 parent_matrix;
		trackerGetMat(tracker, parent_matrix);
		if (splineCollideFull(start, end, parent_matrix, &tracker->def->property_structs.curve->spline, &collide_offset, collide_pos, 10))
		{
			TrackerHandle *handle = trackerHandleCreate(tracker);
			copyVec3(collide_pos, pos);
			eaPush(edObjList, editorObjectCreate(handle, tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER));
			return true;
		}
	}
	for (i = 0; i < tracker->child_count; i++)
		if (wleEdObjCurveClickHelper(start, end, tracker->children[i], edObjList, pos))
			return true;
	return false;
}

/******
* This function handles selection of an object in the world (and NOT the tracker tree).  It is set
* as a callback to the selection harness for determining whether the object will be selected.
* PARAMS:
*   mouseX - int x position of the mouse when user clicked
*   mouseY - int y position of the mouse when user clicked
*   handle - TrackerHandle output of the selected object
* RETURNS:
*   int distance of the intersection from the camera
******/
static float wleEdObjTrackerClickDetect(int mouseX, int mouseY, EditorObject ***edObjs)
{
	int l;
	GfxVisualizationSettings *visSettings = gfxGetVisSettings();
	WorldCollCollideResults results;
	Mat4 cam;
	Vec3 start, end;

	U32 filterBits = WC_FILTER_BIT_EDITOR | WC_FILTER_BIT_TERRAIN;
	if (!editorUIState->disableVolColl)
		filterBits |= WC_FILTER_BIT_VOLUME;	

	gfxGetActiveCameraMatrix(cam);
	editLibCursorSegment(cam, mouseX, mouseY, 10000, start, end);
	worldCollideRay(PARTITION_CLIENT, start, end, filterBits, &results);

	if (results.hitSomething)
	{
		WorldCollisionEntry *entry;
		
		if(wcoGetUserPointer(	results.wco,
								entryCollObjectMsgHandler,
								&entry))
		{
			GroupTracker *tracker = trackerFromTrackerHandle(SAFE_MEMBER(entry, tracker_handle));
			if (tracker && tracker->def && (!tracker->def->property_structs.curve || !tracker->subObjectEditing))
			{
				GroupTracker *clicked = tracker;
				TrackerHandle *handle;
				Vec3 pos;

				// pass base tracker; editor object harness will take care of selecting correct layer of tree
				handle = trackerHandleCreate(tracker);
				eaPush(edObjs, editorObjectCreate(handle, tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER));
				wcoGetPos(results.wco, pos);
				return distance3(cam[3], pos);
			}
			else if (entry && !(entry->filter.shapeGroup == WC_SHAPEGROUP_TERRAIN))
			{
				ZoneMapLayer *layer = worldEntryGetLayer(&entry->base_entry);
				if (layer)
					wleUIViewConfirmDialogCreate(layer);
			}
		}
	}
	for (l = 0; l < zmapGetLayerCount(NULL); l++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, l);
		if (layer)
		{
			Vec3 pos;
			GroupTracker *root_tracker = layerGetTracker(layer);
			if (wleEdObjCurveClickHelper(start, end, root_tracker, edObjs, pos))
				return distance3(cam[3], pos);
		}
	}

	return -1;
}


/******
 * Returns a CRC to use for the tracker tree refresh.
 * PARAMS:
*   edObj - EditorObject containing the TrackerHandle to free
******/
static U32 wleEdObjTrackerCRC(EditorObject *edObj)
{
	TrackerHandle *handle = edObj->obj;
	GroupTracker *tracker = trackerFromTrackerHandle(handle);

	if (!tracker)
		return 0;

	if (tracker->parent && tracker->parent->def)
	{
		GroupChild **parent_def_children = groupGetChildren(tracker->parent->def);
		return parent_def_children[tracker->idx_in_parent]->uid_in_parent;
	}

	cryptAdler32Init();
	cryptAdler32Update((void *)&tracker->idx_in_parent, sizeof(tracker->idx_in_parent));
	if (tracker->def && tracker->def->name_str)
		cryptAdler32UpdateString(tracker->def->name_str);
	return cryptAdler32Final();
}

/******
* This callback handles freeing TrackerHandle EditorObjects.
* PARAMS:
*   edObj - EditorObject containing the TrackerHandle to free
******/
static void wleEdObjTrackerFree(EditorObject *edObj)
{
	StructDestroySafe(parse_GroupProperties, &edObj->props_cpy);
	StructDestroySafe(parse_LogicalGroup, &edObj->logical_grp_cpy);
	edObj->logical_grp_editable = false;
	trackerHandleDestroy(edObj->obj);
}

/******
* This callback handles comparing TrackerHandle EditorObjects.
* PARAMS:
*   edObj1 - EditorObject comparison operand 1
*   edObj2 - EditorObject comparison operand 2
* RETURNS:
*   int comparison value - zero if equal, non-zero if unequal
******/
static int wleEdObjTrackerComp(EditorObject *edObj1, EditorObject *edObj2)
{
	return trackerHandleComp(edObj1->obj, edObj2->obj);
}

/******
* This callback populates an EArray of EditorObject children created from the specified tracker EditorObject.
* PARAMS:
*   edObj - EditorObject of the tracker whose children are to be determined
*   children - EArray of EditorObjects corresponding to each child of the specified tracker EditorObject
******/
static void wleEdObjTrackerChildren(EditorObject *edObj, EditorObject ***children)
{
	GroupTracker *tracker;
	int i;

	assert(edObj->type->objType == EDTYPE_TRACKER);
	tracker = trackerFromTrackerHandle((TrackerHandle*) edObj->obj);
	if (!tracker)
		return;

	if (tracker->def && tracker->def->property_structs.patrol_properties && !EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePatrolPoints", 0))
	{
		for (i = 0; i < eaSize(&tracker->def->property_structs.patrol_properties->patrol_points); i++)
			eaPush(children, wlePatrolPointEdObjCreate(edObj->obj, i));
	}
	else if (tracker->def && tracker->def->property_structs.encounter_properties && !EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideEncounterActors", 0))
	{
		for (i = 0; i < eaSize(&tracker->def->property_structs.encounter_properties->eaActors); i++)
			eaPush(children, wleEncounterActorEdObjCreate(edObj->obj, i));
	}
	else if (tracker->subObjectEditing && tracker->def)
	{
		if (tracker->def->property_structs.curve)
		{
			for (i = 0; i < eafSize(&tracker->def->property_structs.curve->spline.spline_points); i += 3)
				eaPush(children, curveCPCreateEditorObject(edObj->obj, i, tracker->parent_layer ? tracker->parent_layer : edObj->context));
		}
		if (tracker->def->property_structs.curve_gaps)
		{
			for (i = 0; i < eaSize(&tracker->def->property_structs.curve_gaps->gaps); i++)
				eaPush(children, curveGapCreateEditorObject(edObj->obj, i, tracker->parent_layer ? tracker->parent_layer : edObj->context));
		}
	}
	else
	{
		trackerOpen(tracker);
		for (i = 0; i < tracker->child_count; i++)
		{
			if (tracker->children[i] && tracker->children[i]->def)
				eaPush(children, editorObjectCreate(trackerHandleCreate(tracker->children[i]), tracker->children[i]->def->name_str, tracker->parent_layer ? tracker->parent_layer : edObj->context, EDTYPE_TRACKER));
		}
	}
}

/******
* This callback indicates the parent of the specified tracker EditorObject.
* PARAMS:
*   edObj - EditorObject of the tracker whose parent is to be determined
* RETURNS:
*   EditorObject parent of the specified tracker EditorObject
******/
static EditorObject *wleEdObjTrackerParent(EditorObject *edObj)
{
	GroupTracker *tracker;

	assert(edObj->type->objType == EDTYPE_TRACKER);
	tracker = trackerFromTrackerHandle((TrackerHandle*) edObj->obj);
	if (tracker && tracker->parent && tracker->parent->def)
	{
		// return a layer
		if (!tracker->parent->parent)
			return editorObjectCreate(tracker->parent->parent_layer, tracker->parent->def->name_str, tracker->parent->parent_layer, EDTYPE_LAYER);
		else
			return editorObjectCreate(trackerHandleCreate(tracker->parent), tracker->parent->def->name_str, tracker->parent_layer, EDTYPE_TRACKER);
	}
	else
		return NULL;
}

/******
* This does any necessary functionality needed whenever the selection changes in any way (on select
* or deselect).
* PARAMS:
*   selection - TrackerHandle EArray of handles in the new selection
******/
static void wleEdObjTrackerSelectionChanged(EditorObject **selection, bool in_undo)
{
	EditorObject **layers = edObjSelectionGet(EDTYPE_LAYER);
	EditorObject **deselectionList = NULL, **selectionList = NULL;
	bool deselected = false;
	int i;

	// sync selected layers
	// remove selected editor object layers that don't exist anymore
	for (i = eaSize(&layers) - 1; i >= 0; i--)
	{
		ZoneMapLayer *layer = (ZoneMapLayer*) layers[i]->obj;
		int j;	
		bool deselect = true;

		for (j = 0; j < zmapGetLayerCount(NULL); j++)
		{
			ZoneMapLayer *compLayer = zmapGetLayer(NULL, j);
			if (compLayer == layer)
				deselect = false;
		}
		if (deselect)
		{
			eaPush(&deselectionList, layers[i]);
		}
	}

	if (deselectionList)
	{
		deselected = edObjDeselectListEx(deselectionList, false);
		eaDestroy(&deselectionList);
		deselectionList = NULL;
	}

	// select editor object layers whose trackers are selected
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		GroupTracker *tracker = NULL;

		if (layer)
		{
			tracker = layerGetTracker(layer);
		}

		if (tracker && tracker->def && tracker->selected)
		{
			eaPush(&selectionList, editorObjectCreate(layer, tracker->def->name_str, layer, EDTYPE_LAYER));
		}
		else if (tracker)
		{
			eaPush(&deselectionList, editorObjectCreate(layer, tracker->def->name_str, layer, EDTYPE_LAYER));
		}
	}

	if (deselectionList)
	{
		if (edObjDeselectListEx(deselectionList, false))
		{
			deselected = true;
		}
		eaDestroy(&deselectionList);
	}
	if (selectionList)
	{
		edObjSelectList(selectionList, true, true);
		eaDestroy(&selectionList);
	}
	else if (deselected)	//This way, if selection doesn't call the refreshing code, but something was deselected, it gets called here.
	{
		edObjFinishUp();
	}

	// update the move type
	if (eaSize(&selection) == 1)
	{
		GroupTracker *tracker;

		assert(selection[0]->type->objType == EDTYPE_TRACKER);
		tracker = trackerFromTrackerHandle((TrackerHandle*) selection[0]->obj);
		if (tracker && tracker->def)
		{
			if (!tracker->def->property_structs.light_properties)
				editState.trackerType = EditTracker;
			else if (tracker->def->property_structs.light_properties->eLightType == WleAELightSpot)
				editState.trackerType = EditSpotLight;
			else if (tracker->def->property_structs.light_properties->eLightType == WleAELightProjector)
				editState.trackerType = EditProjectorLight;
			else if (tracker->def->property_structs.light_properties->eLightType == WleAELightPoint)
				editState.trackerType = EditPointLight;
			else
				editState.trackerType = EditTracker;
		}
	}
	else
		editState.trackerType = EditTracker;

	if (eaSize(&selection) == 0 && (editState.transformMode == EditTranslateGizmo || editState.transformMode == EditRotateGizmo))
		editState.tempPivotModified = false;

	// ensure current gizmo matches the move type; if not, cycle to a valid gizmo
	if (!wleTransformModeIsValid(editState.transformMode, editState.trackerType))
		wleTransformModeCycle(editState.transformMode, editState.trackerType, false);
}

/******
* This function is a callback for the EditorObject selection framework, dealing with deselection
* of trackers.  This function performs the necessary actions when a tracker is deselected.
* PARAMS:
*   edObj - EditorObject being deselected
******/
static bool wleEdObjTrackerDeselect(EditorObject *edObj)
{
	TrackerHandle *copy;
	GroupTracker *tracker;

	// if locked selection is on, the user cannot change the selection
	if (editState.lockedSelection)
	{
		wleUISelectionLockWarn();
		return false;
	}

	copy = trackerHandleCopy(edObj->obj);

	// quietly allow harness to deselect the handle when its associated tracker doesn't exist
	tracker = trackerFromTrackerHandle(copy);

	// stop rendering wireframe
	trackerSetSelected(tracker, false);

	trackerHandleDestroy(copy);
	return true;
}

/******
* This function handles a newly-selected EditorObject (containing a TrackerHandle).  If the editor
* is in a "select parent" mode (while adding to a group), then the selection executes the
* "add to group" operation, using the specified handle as the new parent.
* PARAMS:
*   handle - TrackerHandle to add to the selection; the added handle will be a copy so this handle
*            should be properly destroyed after this call
******/
static bool wleEdObjTrackerSelect(EditorObject *edObj)
{
	TrackerHandle *copy;
	GroupTracker *tracker;

	copy = trackerHandleCopy(edObj->obj);
	assert(copy);
	tracker = trackerFromTrackerHandle(copy);

	if (!tracker)
		return false;
	else if (editState.mode == EditSelectParent)
	{
		if (editorUIState->showingLogicalTree)
			return false;
		else
		{
			TrackerHandle **handles = NULL;

			// invoke the add to group operation
			editState.mode = EditNormal;
			wleSelectionGetTrackerHandles(&handles);
			wleOpAddToGroup(copy, handles, -1);
			trackerHandleDestroy(copy);
			eaDestroy(&handles);
			return false;
		}
	}
	else if (editState.mode == EditPlaceObjects)
		return false;
	else
	{
		// typical selection path
		// disallow selection of locked/frozen tracker
		if (wleTrackerIsFrozen(copy))
			return false;
		else if (editState.lockedSelection)
		{
			wleUISelectionLockWarn();
			return false;
		}

		// set the open flags as appropriate and render in wireframe
		wleTrackerSelectMain(tracker);
		if (tracker->room && eaSize(&tracker->room->partitions) > 0)
			wleDebugCacheRoomData(tracker->room_partition ? tracker->room_partition : tracker->room->partitions[0]);
		return true;
	}
}

static bool wleSnapToYGetIntersection(Vec3 origVec, Vec3 snappedVec)
{
	WorldCollCollideResults results;
	Vec3 top, bottom;

	U32 filterBits = WC_FILTER_BIT_EDITOR | WC_FILTER_BIT_TERRAIN;
	if (!editorUIState->disableVolColl)
		filterBits |= WC_FILTER_BIT_VOLUME;	

	if (!editState.planesCalculatedThisFrame)
	{
		Mat4 cammat;
		int w, h;
		Vec3 v1, v2, v3;

		gfxGetActiveDeviceSize(&w, &h);
		gfxGetActiveCameraMatrix(cammat);

		// calculate top plane of view frustum
		editLibCursorSegment(cammat, 0, 0, 1000, v1, v2);
		editLibCursorSegment(cammat, w, 0, 1000, v1, v3);
		makePlane(v1, v2, v3, editState.topFrustumPlane);

		// calculate bottom plane of view frustum
		editLibCursorSegment(cammat, 0, h, 1000, v1, v2);
		editLibCursorSegment(cammat, w, h, 1000, v1, v3);
		makePlane(v1, v2, v3, editState.bottomFrustumPlane);

		editState.planesCalculatedThisFrame = true;
	}

	// calculate vertical intersections with frustum planes to determine collision ray
	copyVec3(origVec, top);
	moveVinY(top, unitmat, 10000);
	copyVec3(origVec, bottom);
	moveVinY(bottom, unitmat, -10000);
	intersectPlane(top, bottom, editState.topFrustumPlane, top);
	intersectPlane(top, bottom, editState.bottomFrustumPlane, bottom);

	// check for collisions between the top and bottom points
	worldCollideRay(PARTITION_CLIENT, top, bottom, filterBits, &results);
	if (results.hitSomething)
		copyVec3(results.posWorldImpact, snappedVec);
	return results.hitSomething;
}

/******
* This function calculates the draw matrix for a patrol point, which is used to draw the
* arrows or the X icon.
* PARAMS:
*   patrol - WorldPatrolProperties containing the patrol point to update
*   pointIdx - int index of the point in the patrol
*   worldMat - Mat4 (optional) to transform the position stored in the patrol point priorities
*   outMat - Mat4 output of the calculation
*   snapToY - bool indicating whether to snap the patrol point to the topmost surface in
              view of the camera along the y-axis
*   validDrawMat - bool indicating if moving points' draw_mat have been filled in
*   movingUseWorldMat - bool indicating if moving points' should use the passed in worldMat because they are relative
******/
static void wlePatrolPointGetMat(WorldPatrolProperties *patrol, int pointIdx, Mat4 worldMat, Mat4 outMat, bool snapToY, bool validDrawMat, bool movingUseWorldMat)
{
	Vec3 downvec = {0, -1, 0};
	int nextIdx;
	Vec3 start, end;
	bool endPoint;
	WorldPatrolPointProperties *point;
	WorldPatrolPointProperties *nextPoint;

	assert(pointIdx >= 0 && pointIdx < eaSize(&patrol->patrol_points));
	endPoint = wlePatrolPointIsEndpoint(patrol, pointIdx);

	nextIdx = pointIdx + 1;
	if (nextIdx >= eaSize(&patrol->patrol_points))
		nextIdx = (patrol->route_type == PATROL_CIRCLE ? 0 : (pointIdx - 1));

	if (eaSize(&patrol->patrol_points) == 1)
		nextIdx = pointIdx;

	point = patrol->patrol_points[pointIdx];
	nextPoint = patrol->patrol_points[nextIdx];

	if (worldMat && (movingUseWorldMat || !point->moving))
	{
		mulVecMat4((validDrawMat && point->moving) ? point->draw_mat[3] : point->pos, worldMat, start);
	}
	else
	{
		if (validDrawMat && point->moving)
			copyVec3(point->draw_mat[3], start);
		else
			copyVec3(point->pos, start);
	}
	if(nextIdx != pointIdx)
	{
		if (worldMat && (movingUseWorldMat || !nextPoint->moving))
		{
			mulVecMat4((validDrawMat && nextPoint->moving) ? nextPoint->draw_mat[3] : nextPoint->pos, worldMat, end);
		}
		else 
		{
			if (validDrawMat && nextPoint->moving)
				copyVec3(nextPoint->draw_mat[3], end);
			else
				copyVec3(nextPoint->pos, end);
		}
	}

	// snap start and end points along Y if requested
	if (snapToY)
	{
		// calculate start and end intersections along the Y axis within the view frustum
		wleSnapToYGetIntersection(start, start);
		wleSnapToYGetIntersection(end, end);
	}

	if (nextIdx == pointIdx || nearSameVec3(start, end))
		addVec3(start, forwardvec, end);

	copyVec3(start, outMat[3]);
	subVec3(end, start, outMat[2]);
	normalVec3(outMat[2]);
	if (nearSameVec3(outMat[2], upvec) || nearSameVec3(outMat[2], downvec))
		crossVec3(outMat[2], sidevec, outMat[0]);
	else
		crossVec3(outMat[2], upvec, outMat[0]);
	crossVec3(outMat[2], outMat[0], outMat[1]);
	normalVec3(outMat[0]);
	normalVec3(outMat[1]);
}

/******
* This function refreshes all of the draw matrices for the points in the specified patrol.
* PARAMS:
*   patrol - WorldPatrolProperties to update
*   worldMat - Mat4 (optional) to transform the patrol point positions
*   snapToY - bool indicating whether to snap the patrol points to the topmost surfaces in
*             view of the camera along the y-axis
*	skipMoving - bool indicating if points that are moving should not be refreshed
******/
static void wlePatrolPathRefreshMat(WorldPatrolProperties *patrol, Mat4 worldMat, bool snapToY, bool skipMoving)
{
	int i;
	for (i = 0; i < eaSize(&patrol->patrol_points); i++)
	{
		WorldPatrolPointProperties *point = patrol->patrol_points[i];
		if(skipMoving && point->moving)
			continue;
		wlePatrolPointGetMat(patrol, i, worldMat, point->draw_mat, snapToY, true, !skipMoving);
	}
}

/******
* This function draws the arrow or X icon for the specified patrol point; it does NOT update the
* draw matrix, so this must be done beforehand.
* PARAMS:
*   patrol - WorldPatrolProperties containing the point to draw
*   pointIdx - int index of the point in the patrol
*   highlight - bool indicating whether to draw the patrol point in wireframe (i.e. selected)
******/
static void wlePatrolPointDraw(WorldPatrolProperties *patrol, int pointIdx, Mat4 pointMat, bool highlight)
{
	TempGroupParams tgparams = {0};
	GroupDef *drawDef;
	
	assert(pointIdx >= 0 && pointIdx < eaSize(&patrol->patrol_points));

	tgparams.no_culling = true;
	if (highlight)
		tgparams.wireframe = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedGeo", 1) ? 1 : 2;

	drawDef = wlePatrolPointIsEndpoint(patrol, pointIdx) ? editState.pointDef : editState.arrowDef;
	worldAddTempGroup(drawDef, pointMat, &tgparams, true);
}

/******
* This function draws the specified patrol; it does NOT update the points' draw matrices, so
* this must be done beforehand.
* PARAMS:
*   patrol - WorldPatrolProperties containing the point to draw
*	drawPoints - bool indicating whether to draw the point icons along the patrol or just the patrol lines
*   highlight - bool indicating whether to draw the patrol points in wireframe (i.e. selected) and 
*               the lines in white
******/
static void wlePatrolPathDraw(WorldPatrolProperties *patrol, bool drawPoints, bool highlight, Mat4 worldMat)
{
	Vec3 firstPoint, point, lastVec;
	int i;

	for (i = 0; i < eaSize(&patrol->patrol_points); i++)
	{
		if (drawPoints && !patrol->patrol_points[i]->moving)
			wlePatrolPointDraw(patrol, i, patrol->patrol_points[i]->draw_mat, highlight);

		if (drawPoints && patrol->patrol_points[i]->moving)
			copyVec3(patrol->patrol_points[i]->draw_mat[3], point);
		else
			mulVecMat4(patrol->patrol_points[i]->pos, worldMat, point);

		if (i > 0)
			gfxDrawLine3D(lastVec, point, highlight ? ColorWhite : ColorRed);
		if (i == 0)
			copyVec3(point, firstPoint);
		copyVec3(point, lastVec);
	}
	if (patrol->route_type == PATROL_CIRCLE)
		gfxDrawLine3D(lastVec, firstPoint, highlight ? ColorWhite : ColorRed);
}

static void wleEncounterActorRefreshMat(WorldActorProperties *actor, Mat4 worldMat, bool snapToY)
{
	Mat4 actorMat;

	createMat3YPR(actorMat, actor->vRot);
	copyVec3(actor->vPos, actorMat[3]);

	if (worldMat)
		mulMat4(worldMat, actorMat, actor->draw_mat);
	else
		copyMat4(actorMat, actor->draw_mat);

	if (snapToY)
		wleSnapToYGetIntersection(actor->draw_mat[3], actor->draw_mat[3]);
}

bool wleIsActorDisabled(WorldEncounterProperties *encounter, WorldActorProperties *actor)
{
	if (GET_REF(encounter->hTemplate)) 
	{
		EncounterTemplate *pTemplate = GET_REF(encounter->hTemplate);
		EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, encounter, actor);

		if(pActor)
			return !encounterTemplate_GetActorEnabled(pTemplate, pActor, gEditorTeamSize, wleGetEncounterDifficulty(encounter));
	}
	return true;
}

static void wleEncounterActorDraw(Mat4 actorMat, bool highlight, bool disabled)
{
	TempGroupParams tgparams = {0};

	tgparams.no_culling = true;
	if (highlight)
		tgparams.wireframe = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedGeo", 1) ? 1 : 2;

	if (disabled)
		worldAddTempGroup(editState.actorDisabledDef, actorMat, &tgparams, true);
	else
		worldAddTempGroup(editState.actorDef, actorMat, &tgparams, true);
}

#endif

// Used by the server to update the actor indices on fillActorsInOrder encounters
AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(9);
void encounter_UpdateActorIndex(const char* pchEncounter, const char* pchActor, int iActorIndex, int bActorIndexSet)
{
#ifndef NO_EDITORS
	EditorObject **actorSelection = edObjSelectionGet(EDTYPE_ENCOUNTER_ACTOR);
	int i,j;

	if(!editState.encounterTrackers || !pchActor || !pchEncounter)
		return;

	for(i = eaSize(&editState.encounterTrackers)-1; i >= 0; i--)
	{
		GroupTracker *pTracker = trackerFromTrackerHandle(editState.encounterTrackers[i]);
		GroupDef *pDef = pTracker ? pTracker->def : NULL;
		WorldEncounterProperties *encounterProperties = pDef ? pDef->property_structs.encounter_properties : NULL;
		const char* pchSelectedEncounter = pTracker && pTracker->closest_scope ? trackerGetUniqueZoneMapScopeName(pTracker) : NULL;

		if(encounterProperties && stricmp(pchEncounter, pchSelectedEncounter) == 0)
		{
			if(!encounterProperties->bFillActorsInOrder)
				return;
			
			for(j=eaSize(&encounterProperties->eaActors)-1; j>=0; j--)
			{
				WorldActorProperties *actor = encounterProperties->eaActors[j];
				if(actor && stricmp(actor->pcName, pchActor) == 0)
				{
					actor->bActorIndexSet = bActorIndexSet;
					actor->iActorIndex = iActorIndex;
					break;
				}
			}
			break;
		}
	}
#endif
	return;
}

#ifndef NO_EDITORS

/******
* This function draws editor-specific features for the specified tracker/def.
* PARAMS:
*   def - GroupDef to draw
*   tracker - GroupTracker to draw
*   snapToY - bool indicating whether to draw patrol defs with their points snapped along
*             the y-axis
******/
static void wleEdObjTrackerDrawRecursive(GroupDef *def, GroupTracker *tracker, bool snapToY, bool drawWireframes)
{
	GroupChild **defChildren;
	Mat4 mat;
	//bool drawWireframes = ;
	int i;

	if (!def || !tracker)
		return;

	assert(tracker->def == def);

	if (def->property_structs.sound_sphere_properties || groupHasLight(def) || def->property_structs.wind_source_properties)
		trackerGetMat(tracker, mat);

	if (drawWireframes && wlSoundRadiusFunc && def->property_structs.sound_sphere_properties)
	{
		const char *name = SAFE_MEMBER(def->property_structs.sound_sphere_properties, pcEventName);
		if (name)
			gfxDrawSphere3D(mat[3], wlSoundRadiusFunc(name), EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "NumSegments", -1), ColorWhite, 1);
		// Draw connection info
		{
			WorldCellEntry *ent;
			WorldVolumeEntry *vent = NULL;

			for(i=0; i<eaSize(&tracker->cell_entries); i++)
			{
				ent = tracker->cell_entries[i];

				if(ent->type==WCENT_VOLUME)
				{
					vent = (WorldVolumeEntry*)ent;
					break;
				}
			}
		}
	}

	if (drawWireframes && groupHasLight(def) )
	{
		GroupDef **defs = trackerGetDefChain(tracker);
		LightData *light_data = groupGetLightData(defs, mat);
		gfxDrawLightWireframe(light_data, EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "NumSegments", -1));
		StructDestroySafe(parse_LightData, &light_data);
		eaDestroy(&defs);
	}

	if (drawWireframes && def->property_structs.wind_source_properties)
	{
		int num_segs = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "NumSegments", -1);
		
		gfxDrawSphere3D(mat[3], def->property_structs.wind_source_properties->radius, num_segs, ColorWhite, 1);
		
		if (def->property_structs.wind_source_properties->radius_inner)
			gfxDrawSphere3D(mat[3], def->property_structs.wind_source_properties->radius_inner, num_segs, ColorWhite, 1);
	}

	defChildren = groupGetChildren(def);
	for (i = 0; i < eaSize(&defChildren); ++i)
	{
		GroupTracker *childTracker = tracker->children ? tracker->children[i] : NULL;
		GroupDef *child_def = groupChildGetDef(def, defChildren[i], true);
		if (child_def)
			wleEdObjTrackerDrawRecursive(child_def, childTracker, snapToY, !!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedWireframe", 1));
	}
}


/******
* This function is called once per frame by the Editor Object framework.  It is given the current
* selection of editor objects.  We use it to draw the bounding box of selected objects.
* PARAMS:
*   selection - EditorObject EArray of what is currently selected
******/
static void wleEdObjTrackerSelectionDraw(EditorObject **selection)
{
	TempGroupParams tgparams = {0};
	int i;

	curveEditorDrawFunction(selection);
	tgparams.no_culling = true;
	for (i = 0; i < eaSize(&selection); i++)
	{
		TrackerHandle *handle = selection[i]->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(handle);

		if (!tracker || !tracker->def)
			continue;

		if (EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedBounds", 0) && !editState.drawGhosts)
		{
			Mat4 trackerMat;
			Vec3 minScaled, maxScaled;

			trackerGetMat(tracker, trackerMat);
			scaleVec3(tracker->def->bounds.min, tracker->scale, minScaled);
			scaleVec3(tracker->def->bounds.max, tracker->scale, maxScaled);
			gfxDrawBox3D(minScaled, maxScaled, trackerMat, ColorWhite, 1);
		}

		if (editState.drawGhosts)
		{
			if (editState.inputData.moveCopyKey)
				wleEdObjTrackerDrawRecursive(tracker->def, tracker, editState.mode == EditPlaceObjects, !!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedWireframe", 1));
			tracker = worldAddTempGroup(tracker->def, selection[i]->mat, &tgparams, true);
		}
		if (tracker)
			wleEdObjTrackerDrawRecursive(tracker->def, tracker, editState.mode == EditPlaceObjects, !!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedWireframe", 1));
	}
}

/******
* This is the callback that allows the EditorObject framework to retrieve a matrix for each editor
* object.
* PARAMS:
*   edObj - EditorObject whose matrix is being queried
*   outMat - Mat4 result
******/
static void wleEdObjTrackerGetMat(EditorObject *edObj, Mat4 outMat)
{
	GroupTracker *tracker = trackerFromTrackerHandle(edObj->obj);
	if (tracker)
		trackerGetMat(tracker, outMat);
	else
		copyMat4(unitmat, outMat);
}

/******
* This is the callback that allows the EditorObject framework to center the camera on an object based
* on its bounds.
* PARAMS:
*   edObj - EditorObject whose bounds are being queried
*   min - Vec3 min corner of the axis-aligned bounding box
*   max - Vec3 max corner of the axis-aligned bounding box
******/
static void wleEdObjTrackerGetBounds(EditorObject *edObj, Vec3 min, Vec3 max)
{
	GroupTracker *tracker = trackerFromTrackerHandle(edObj->obj);
	if (tracker && tracker->def)
	{
		Mat4 mat;
		trackerGetMat(tracker, mat);
		mulBoundsAA(tracker->def->bounds.min, tracker->def->bounds.max, mat, min, max);
	}
	else
	{
		copyVec3(zerovec3, min);
		copyVec3(zerovec3, max);
	}
}

/******
* This is a callback to the Editor Object framework, invoked when an object begins to move (i.e. the
* user clicks on the gizmo.  The function simply sets the objects to be invisible so that their ghosts can
* be drawn instead.
* PARAMS:
*   selection - EditorObject EArray of the selected objects
******/
static void wleEdObjTrackerStartMove(EditorObject **selection)
{
	int i;
	GroupTracker *tracker;

	// we only draw ghosts if the user is not editing the pivot temporarily
	editState.drawGhosts = (editState.gizmoMode == EditActual || editState.mode == EditPlaceObjects);

	// we render the moving objects invisible while in motion
	for (i = 0; i < eaSize(&selection); i++)
	{
		tracker = trackerFromTrackerHandle(selection[i]->obj);
		if (editState.drawGhosts && !editState.inputData.moveCopyKey)
			trackerSetInvisible(tracker, true);
	}
}

static void wleEdObjMovingUpdateAEPosRot(EditorObject **movingObjs)
{
	EditorObject *aeObj = wleAEGetSelected();
	int i;

	// do not update the placement panel if unnecessary
	if (!aeObj || aeObj == editorUIState->trackerTreeUI.allSelect || editState.gizmoMode == EditPivotTemp || (editState.gizmoMode == EditPivot && eaSize(&movingObjs) > 1))
		return;

	for (i = 0; i < eaSize(&movingObjs); i++)
	{
		EditorObject *movingObj = movingObjs[i];
		if (edObjIsDescendant(movingObj, aeObj))
		{
			Mat4 movingMat, aeMat, inv, offset;

			// this means that the attribute editor selection is also being moved, so we update its
			// information by calculating the offset matrix for the transformation and applying it
			// to the attribute editor selection's matrix
			edObjGetMatrix(movingObj, movingMat);
			edObjGetMatrix(aeObj, aeMat);
			invertMat4(movingMat, inv);
			mulMat4(movingObj->mat, inv, offset);
			mulMat4(offset, aeMat, movingMat);
			wleAEPlacementPosRotUpdate(movingMat);
			return;
		}
	}
}

/******
* This callback is invoked by the EditorObject harness every frame while one of the EditorObject
* transformation gizmos is active.  We use this function primarily to update the attribute editor.
* PARAMS:
*   selection - EditorObject EArray of the selected objects
******/
static void wleEdObjTrackerMoving(EditorObject **selection)
{
	// refresh the attribute editor placement info, if necessary
	wleEdObjMovingUpdateAEPosRot(selection);
}

/******
* This callback is invoked by the EditorObject harness when a gizmo transformation ends (i.e. when the
* user stops a gizmo mouse drag).  It is used to actually perform the movement operation.
* PARAMS:
*   selection - EditorObject EArray of the selected objects
******/
static void wleEdObjTrackerEndMove(EditorObject **selection)
{
	TrackerHandle **handles = NULL;

	// we never need to perform a real move operation whenever the user is moving the pivot temporarily
	if (editState.mode != EditPlaceObjects &&
		(editState.gizmoMode == EditPivotTemp || (editState.gizmoMode == EditPivot && eaSize(&selection) > 1)))
	{
		editState.tempPivotModified = true;
		return;
	}

	// if moving the pivot (on a single tracker; moving pivot of multiple trackers behaves like
	// moving pivot temporarily) instead of the actual selection
	else
	{
		Mat4 *mats = NULL;
		int i, matCount = 0, matMaxCount = 0;

		dynArrayAddStructs(mats, matCount, matMaxCount, eaSize(&selection));
		assert(mats);
		for (i = 0; i < eaSize(&selection); i++)
		{
			eaPush(&handles, trackerHandleCopy(selection[i]->obj));
			if (editState.mode != EditPlaceObjects && editState.gizmoMode == EditPivot)
				edObjHarnessGetGizmoMatrix(mats[i]);
			else
				copyMat4(selection[i]->mat, mats[i]);
		}

		// rerender the invisible trackers
		for (i = 0; i < eaSize(&handles); i++)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(handles[i]);
			trackerSetInvisible(tracker, false);
		}
		if (eaSize(&handles) > 0)
		{
			EditUndoBeginGroup(edObjGetUndoStack());

			// duplicate objects first if doing a move copy
			if (editState.inputData.moveCopyKey && editState.gizmoMode == EditActual)
			{
				EditorObject **newSelection;

				trackerActionDuplicate(selection);
				newSelection = edObjSelectionGet(EDTYPE_TRACKER);
				eaDestroyEx(&handles, trackerHandleDestroy);
				for (i = 0; i < eaSize(&newSelection); i++)
					eaPush(&handles, trackerHandleCopy(newSelection[i]->obj));
			}

			wleTrackersMoveToMats(handles, mats, editState.mode == EditPlaceObjects);
			EditUndoEndGroup(edObjGetUndoStack());
		}

		eaDestroyEx(&handles, trackerHandleDestroy);
		SAFE_FREE(mats);
	}

	editState.drawGhosts = false;
}

/******
* This function determines whether the editor object harness's movement gizmo should be enabled.
* PARAMS:
*   edObj - EditorObject for which to check whether movement is allowed for it
* RETURNS:
*   bool indicating whether movement is allowed for the specified EditorObject
******/
bool wleEdObjTrackerMovementEnable(EditorObject *edObj, EditorObjectGizmoMode mode)
{
	return (editState.transformMode == EditTranslateGizmo || editState.transformMode == EditRotateGizmo);
}

/******
* This function returns the CRC for a layer, which is identical to the CRC for the layer's tracker, if
* it has one.
* PARAMS:
*   edObj - EditorObject for the layer whose CRC is being returned
* RETURNS:
*   U32 layer's CRC
******/
static U32 wleEdObjLayerCRC(EditorObject *edObj)
{
	ZoneMapLayer *layer = edObj->obj;
	GroupTracker *tracker = layerGetTracker(layer);

	if (!tracker)
		return (intptr_t) layer;

	cryptAdler32Init();
	cryptAdler32Update((void *)&tracker->idx_in_parent, sizeof(tracker->idx_in_parent));
	if (tracker->def && tracker->def->name_str)
		cryptAdler32UpdateString(tracker->def->name_str);
	return cryptAdler32Final();
}

/******
* This function populates a list of a specified layer's child EditorObjects.
* PARAMS:
*   object - EditorObject for the layer
*   children - EArray of EditorObjects that will be populated with the children of the specified
*              layer
******/
static void wleEdObjLayerChildren(EditorObject *object, EditorObject ***children)
{
	int i;
	ZoneMapLayer *layer = (ZoneMapLayer*)object->obj;
	GroupTracker *tracker;

	assert(object->type->objType == EDTYPE_LAYER);
	if (!layer)
		return;

	tracker = layerGetTracker(layer);
	if (!tracker)
		return;

	for (i = 0; i < tracker->child_count; i++)
	{
		GroupDef *def = tracker->children[i]->def;
		TrackerHandle *tempHandle = trackerHandleFromTracker(tracker->children[i]);
		if (tempHandle && wleUITrackerNodeFilterCheck(tempHandle))
		{
			trackerOpen(tracker->children[i]);
			eaPush(children, editorObjectCreate(trackerHandleCreate(tracker->children[i]), (def && def->name_str) ? def->name_str : "DELETED", tracker->parent_layer ? tracker->parent_layer : layer, EDTYPE_TRACKER));
		}
	}
}

/******
* This function compares two layer EditorObjects.
* PARAMS:
*   obj1 - EditorObject layer to compare
*   obj2 - EditorObject layer to compare
* RETURNS:
*   int value of comparison
******/
static int wleEdObjLayerCompare(EditorObject *obj1, EditorObject *obj2)
{
	ZoneMapLayer *layer1, *layer2;
	if (obj1->type != obj2->type)
		return -1;

	assert(obj1->type == obj2->type);
	assert(obj1->type == editorObjectTypeGet(EDTYPE_LAYER));
	layer1 = (ZoneMapLayer*) obj1->obj;
	layer2 = (ZoneMapLayer*) obj2->obj;
	return (intptr_t)layer1 - (intptr_t)layer2;
}

/******
* This function handles syncing layer tracker selections with the layer selection and is called
* automatically by the selection harness when the selection changes.
* PARAMS:
*   edObjs - EditorObject EArray of layer objects that are selected
******/
static void wleEdObjLayerSelectionChanged(EditorObject **edObjs, bool in_undo)
{
	int i;
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		GroupTracker *tracker = NULL;

		if (layer)
			tracker = layerGetTracker(layer);

		if (tracker && tracker->def && tracker->selected != (U32)!!edObjFindSelected(editorObjectCreate(layer, NULL, layer, EDTYPE_LAYER)))
		{
			if (tracker->selected)
				edObjDeselect(editorObjectCreate(trackerHandleCreate(tracker), tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER));
			else
				edObjSelect(editorObjectCreate(trackerHandleCreate(tracker), tracker->def->name_str, tracker->parent_layer, EDTYPE_TRACKER), true, true);
		}
	}
}

/******
* This function handles the selection of a layer.  This is called when a layer object is selected
* by any means.
* PARAMS:
*   edObj - EditorObject layer that was selected
* RETURNS:
*   bool indicating whether the selection should be allowed to continue
******/
static bool wleEdObjLayerSelect(EditorObject *edObj)
{
	ZoneMapLayer *layer = (ZoneMapLayer*) edObj->obj;

	if (!layer)
		return false;

	if (editState.mode == EditSelectParent)
	{
		TrackerHandle *parent = trackerHandleCreate(layerGetTracker(layer));
		TrackerHandle **handles = NULL;

		if (!parent)
			return false;

		// invoke the add to group operation
		editState.mode = EditNormal;
		wleSelectionGetTrackerHandles(&handles);
		wleOpAddToGroup(parent, handles, -1);
		trackerHandleDestroy(parent);
		eaDestroy(&handles);
		return false;
	}

	return true;
}

void wleEdObjLayerActionLock(EditorObject **selection)
{
	int i;
	for (i = 0; i < eaSize(&selection); i++)
	{
		ZoneMapLayer *layer = selection[i]->context;
		if (!layerGetLocked(layer))
			wleUILockLayerWrapper(layer);
	}
}

void wleEdObjLayerActionRevert(EditorObject **selection)
{
	int i;
	for (i = 0; i < eaSize(&selection); i++)
	{
		ZoneMapLayer *layer = selection[i]->context;
		if (layerGetLocked(layer))
		{
			edObjSelectionClear(EDTYPE_NONE);
			wleUIUnlockLayerWrapper(layer);
			layerReload(layer, false);
		}
	}
}

void wleEdObjLayerActionSave(EditorObject **selection)
{
	int i;
	for (i = 0; i < eaSize(&selection); i++)
	{
		ZoneMapLayer *layer = selection[i]->context;
		if (layerGetLocked(layer))
			layerSave(layer, false, true);
	}
	EditUndoStackClear(edObjGetUndoStack());
}

void wleEdObjLayerActionSaveAndClose(EditorObject **selection)
{
	int i;
	for (i = 0; i < eaSize(&selection); i++)
	{
		ZoneMapLayer *layer = selection[i]->context;
		if (layerGetLocked(layer))
		{
			layerSave(layer, false, true);
			wleUIUnlockLayerWrapper(layer);
		}
	}
	EditUndoStackClear(edObjGetUndoStack());
}

static EditorObject *wleEncObjFXTargetNodeParent(EditorObject *edObj)
{
	GroupTracker *tracker = trackerFromTrackerHandle(edObj->obj);

	if (!tracker)
		return NULL;

	return editorObjectCreate(trackerHandleCreate(tracker), (tracker->def && tracker->def->name_str) ? tracker->def->name_str : "", tracker->parent_layer, EDTYPE_TRACKER);
}

static int wleEdObjFXTargetNodeComp(EditorObject *edObj1, EditorObject *edObj2)
{
	//There should only ever be one target node, so it is always equal to itself
	return 0;
}

static void wleEdObjFXTargetNodeGetMat(EditorObject *edObj, Mat4 mat)
{
	GroupTracker *tracker = trackerFromTrackerHandle(edObj->obj);
	if(tracker) {
		Mat4 childMat, parentMat;

		if(tracker->def->property_structs.fx_properties) {
			copyVec3(tracker->def->property_structs.fx_properties->vTargetPos, childMat[3]);
			createMat3YPR(childMat, tracker->def->property_structs.fx_properties->vTargetPyr);
		} else {
			identityMat4(childMat);
		}
			
		trackerGetMat(tracker, parentMat);
		mulMat4(parentMat, childMat, mat);
	} else {
		identityMat4(mat);
	}
}

static void wleEdObjFXTargetNodeMoving(EditorObject **selection)
{
	int i;
	for ( i=0; i < eaSize(&selection); i++ )
	{
		GroupTracker *tracker = trackerFromTrackerHandle(selection[i]->obj);
		if(tracker)
		{
			Color color;
			Mat4 cam, mat;
			Vec3 boxMin, boxMax;
			F32 fardist;

			edObjHarnessGetGizmoMatrix(mat);
			gfxGetActiveCameraMatrix(cam);

			fardist = distance3(cam[3], mat[3]);
			fardist *= 0.01f; 
			setVec3same(boxMin, -fardist);
			setVec3same(boxMax,  fardist);
			setVec4(color.rgba, 0xFF, 0xFF, 0x00, 0xFF);

			gfxDrawBox3D(boxMin, boxMax, mat, color, 0);
		}
	}
}

static void wleEdObjFXTargetNodeEndMove(EditorObject **selection)
{
	int i;
	for ( i=0; i < eaSize(&selection); i++ )
	{
		GroupTracker *tracker = wleOpPropsBegin(selection[i]->obj);
		if(tracker)
		{
			Mat4 childMat, parentMat, retMat;

			edObjHarnessGetGizmoMatrix(childMat);
			trackerGetMat(tracker, retMat);
			invertMat4(retMat, parentMat);
			mulMat4(parentMat, childMat, retMat);

			if(!tracker->def->property_structs.fx_properties)
				tracker->def->property_structs.fx_properties = StructCreate(parse_WorldFXProperties);
			copyVec3(retMat[3], tracker->def->property_structs.fx_properties->vTargetPos);
			getMat3YPR(retMat, tracker->def->property_structs.fx_properties->vTargetPyr);

			wleOpPropsUpdate();
			wleOpPropsEnd();
		}
	}
}

// subobject callbacks and helper functions
static U32 wleEncObjSubHandleCRC(EditorObject *edObj)
{
	WleEncObjSubHandle *handle = (WleEncObjSubHandle*) edObj->obj;
	U32 startCRC = 0;

	GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);

	if (tracker && tracker->parent && tracker->parent->def)
	{
		GroupChild **parent_def_children = groupGetChildren(tracker->parent->def);
		startCRC = parent_def_children[tracker->idx_in_parent]->uid_in_parent;
	}

	cryptAdler32Init();
	cryptAdler32Update((void*) &startCRC, sizeof(startCRC));
	cryptAdler32Update((void*) &handle->childIdx, sizeof(handle->childIdx));
	return cryptAdler32Final();	
}

static void wleEncObjSubHandleEdObjDestroy(EditorObject *edObj)
{
	wleEncObjSubHandleDestroy(edObj->obj);
}

static int wleEncObjSubHandleComp(EditorObject *edObj1, EditorObject *edObj2)
{
	WleEncObjSubHandle *handle1 = edObj1->obj;
	WleEncObjSubHandle *handle2 = edObj2->obj;
	int ret = trackerHandleComp(handle1->parentHandle, handle2->parentHandle);

	if (ret)
		return ret;
	else
		return handle1->childIdx - handle2->childIdx;
}

static EditorObject *wleEncObjSubHandleParent(EditorObject *edObj)
{
	WleEncObjSubHandle *handle = (WleEncObjSubHandle*) edObj->obj;
	GroupTracker *tracker;

	if (!handle || !handle->parentHandle)
		return NULL;

	tracker = trackerFromTrackerHandle(handle->parentHandle);
	if (!tracker)
		return NULL;

	return editorObjectCreate(trackerHandleCreate(tracker), (tracker->def && tracker->def->name_str) ? tracker->def->name_str : "", tracker->parent_layer, EDTYPE_TRACKER);
}

static bool wleEncObjSubHandleMovementEnable(EditorObject *edObj, EditorObjectGizmoMode mode)
{
	return true;
}

static bool wleEncObjSubHandleSelect(EditorObject *edObj)
{
	WleEncObjSubHandle *handle = edObj->obj;

	if (!handle)
		return false;

	if (wleTrackerIsFrozen(handle->parentHandle))
		return false;
	else if (editState.lockedSelection)
	{
		wleUISelectionLockWarn();
		return false;
	}

	return true;
}

static bool wleEncObjSubHandleDeselect(EditorObject *edObj)
{
	WleEncObjSubHandle *handle = edObj->obj;

	if (!handle)
		return false;

	if (editState.lockedSelection)
	{
		wleUISelectionLockWarn();
		return false;
	}

	return true;
}

// patrol point callbacks
static bool wleEdObjPatrolPointSelect(EditorObject *edObj)
{
	bool ret = wleEncObjSubHandleSelect(edObj);
	if (ret)
	{
		WorldPatrolPointProperties *point = wlePatrolPointFromHandle(edObj->obj, NULL);
		if (point)
			point->selected = 1;
	}

	return ret;
}

static bool wleEdObjPatrolPointDeselect(EditorObject *edObj)
{
	bool ret = wleEncObjSubHandleDeselect(edObj);
	if (ret)
	{
		WorldPatrolPointProperties *point = wlePatrolPointFromHandle(edObj->obj, NULL);
		if (point)
			point->selected = 0;
	}

	return ret;
}

static void wleEdObjPatrolPointDraw(EditorObject **selection)
{
	int i;

	for (i = 0; i < eaSize(&selection); i++)
	{
		Mat4 pointMat;
		WleEncObjSubHandle *subHandle = selection[i]->obj;
		WorldPatrolProperties *patrol = wlePatrolFromHandle(subHandle, NULL);
		WorldPatrolPointProperties *point = wlePatrolPointFromHandle(subHandle, pointMat);
		if(patrol && point)
		{
			if(point->moving)
			{
				//Draw Old
				if(editState.inputData.moveCopyKey)
					wlePatrolPointDraw(patrol, subHandle->childIdx, pointMat, true);
				//Draw New
				wlePatrolPointDraw(patrol, subHandle->childIdx, point->draw_mat, false);
			}
			else
			{
				wlePatrolPointDraw(patrol, subHandle->childIdx, pointMat, true);
			}
		}
	}
}

float wlePatrolPointCollide(Vec3 start, Vec3 end, Vec3 intersection, WleEncObjSubHandle **object)
{
	Mat4 camMat;
	float closestDist = -1, dist;
	int i, j;

	// shouldn't hit patrol points if they're hidden
	if (EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePatrolPoints", 0))
		return -1;

	// this is assumed, but we set it to NULL anyway
	if (object)
		*object = NULL;

	gfxGetActiveCameraMatrix(camMat);

	for (i = 0; i < eaSize(&editState.patrolTrackers); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(editState.patrolTrackers[i]);
		if (tracker && tracker->def && tracker->def->property_structs.patrol_properties)
		{
			WorldPatrolProperties *patrol = tracker->def->property_structs.patrol_properties;
			Mat4 boxInvMat, patrolMat, pointMat;
			Vec3 startLocal, endLocal, intersect, nextHit;
			trackerGetMat(tracker, patrolMat);

			for (j = 0; j < eaSize(&patrol->patrol_points); j++)
			{
				bool isX = wlePatrolPointIsEndpoint(patrol, j);

				// transform ray into local coords
				wlePatrolPointGetMat(patrol, j, patrolMat, pointMat, false, false, true);
				invertMat4(pointMat, boxInvMat);
				mulVecMat4(start, boxInvMat, startLocal);
				mulVecMat4(end, boxInvMat, endLocal);

				// retransform intersection to world coords
				if (lineBoxCollision(startLocal, endLocal, isX ? wlePatrolEndMin : wlePatrolArrowMin, isX ? wlePatrolEndMax : wlePatrolArrowMax, intersect))
				{
					mulVecMat4(intersect, pointMat, nextHit);

					// check if closest to cam thus far
					dist = distance3(camMat[3], nextHit);
					if (closestDist == -1 || dist < closestDist)
					{
						closestDist = dist;
						if (intersection)
							copyVec3(nextHit, intersection);
						if (object)
						{
							if (*object)
								wleEncObjSubHandleDestroy(*object);
							*object = wleEncObjSubHandleCreate(editState.patrolTrackers[i], j);
						}
					}
				}
			}
		}
	}

	return closestDist;
}

static float wleEdObjPatrolPointClickDetect(int mouseX, int mouseY, EditorObject ***edObjs)
{
	WleEncObjSubHandle *handle = NULL;
	Vec3 start, end;
	float closestDist;

	editLibCursorRay(start, end);
	closestDist = wlePatrolPointCollide(start, end, NULL, &handle);

	if (closestDist != -1 && handle)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);
		char name[128];

		if (tracker)
		{
			sprintf(name, "Point %i", handle->childIdx);
			eaPush(edObjs, editorObjectCreate(handle, name, tracker->parent_layer, EDTYPE_PATROL_POINT));
		}
	}

	return closestDist;
}

static void wleEdObjPatrolPointMarqueeDetect(int mouseX, int mouseY, int mouseX2, int mouseY2, int depth, Mat4 cam, Mat44 scrProjMat, EditorObject ***edObjs, bool crossingMode)
{
	WleFilter *filter = ui_ComboBoxGetSelectedObject(editorUIState->toolbarUI.marqueeFilterCombo);
	Vec3 marqueeMin, marqueeMax;
	Vec3 bottomLeft, topRight, camDir;
	Vec4_aligned boxVerts[8];
	int i, j, k;

	// can't select points if they're hidden
	if (EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePatrolPoints", 0))
		return;

	marqueeMin[0] = mouseX;
	marqueeMin[1] = mouseY;
	marqueeMin[2] = 0;
	marqueeMax[0] = mouseX2;
	marqueeMax[1] = mouseY2;
	marqueeMax[2] = depth;
	scaleVec3(cam[2], -1, camDir);

	for (i = 0; i < eaSize(&editState.patrolTrackers); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(editState.patrolTrackers[i]);
		if (tracker &&
			(tracker->open || (filter && filter->ignoreNodeState)) &&
			tracker->enc_obj && tracker->enc_obj->type == WL_ENC_PATROL_ROUTE)
		{
			WorldPatrolRoute *patrol = (WorldPatrolRoute*) tracker->enc_obj;
			for (j = 0; j < eaSize(&patrol->properties->patrol_points); j++)
			{
				bool isX = wlePatrolPointIsEndpoint(patrol->properties, j);

				// project the patrol point's bounding box onto the screen and get the axis-aligned bounding box of the
				// projected vertices
				editLibFindScreenCoords(isX ? wlePatrolEndMin : wlePatrolArrowMin, isX ? wlePatrolEndMax : wlePatrolArrowMax, patrol->properties->patrol_points[j]->draw_mat, scrProjMat, bottomLeft, topRight);
				mulBounds(isX ? wlePatrolEndMin : wlePatrolArrowMin, isX ? wlePatrolEndMax : wlePatrolArrowMax, patrol->properties->patrol_points[j]->draw_mat, boxVerts);

				// we also calculate distances from each bounding vertex to the plane that intersects the
				// camera whose normal is the camera direction - this allows us to determine the "z" bound
				// so that objects that are behind the camera or too far in front won't get selected
				for (k = 0; k < 8; k++)
				{
					Vec3 vertDir;
					float distFromCamPlane;

					subVec3(boxVerts[k], cam[3], vertDir);
					distFromCamPlane = dotVec3(camDir, vertDir);
					if (!k || bottomLeft[2] > distFromCamPlane)
						bottomLeft[2] = distFromCamPlane;
					if (!k || topRight[2] < distFromCamPlane)
						topRight[2] = distFromCamPlane;
				}

				// if the current point is bound by our marquee (in non-crossing mode) or touches our marquee (in crossing mode), then add it to the selection
				if ((crossingMode && boxBoxCollision(marqueeMin, marqueeMax, bottomLeft, topRight)) || (!crossingMode && wleBoxContainsAA(marqueeMin, marqueeMax, bottomLeft, topRight)))
				{
					WleEncObjSubHandle *handle = wleEncObjSubHandleCreate(trackerHandleFromTracker(tracker), j);

					if (handle)
					{
						WleFilter *marqueeFilter = ui_ComboBoxGetSelectedObject(editorUIState->toolbarUI.marqueeFilterCombo);
						char name[128];
						EditorObject *obj;
						
						sprintf(name, "Point %i", j);
						obj = editorObjectCreate(handle, name, tracker->parent_layer, EDTYPE_PATROL_POINT);

						if (wleFilterApply(obj, marqueeFilter))
							eaPush(edObjs, obj);
						else
							editorObjectDeref(obj);
					}
				}
			}
		}
	}
}

static void wleEdObjPatrolPointGetMat(EditorObject *edObj, Mat4 mat)
{
	WleEncObjSubHandle *handle = edObj->obj;
	GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);
	WorldPatrolPointProperties *point = NULL;
	Mat4 trackerMat;

	if (tracker && tracker->def && tracker->def->property_structs.patrol_properties && handle->childIdx >= 0 && handle->childIdx < eaSize(&tracker->def->property_structs.patrol_properties->patrol_points))
		point = tracker->def->property_structs.patrol_properties->patrol_points[handle->childIdx];

	copyMat4(unitmat, mat);
	if (tracker && point)
	{
		trackerGetMat(tracker, trackerMat);
		mulVecMat4(point->pos, trackerMat, mat[3]);
	}
}

static void wleEdObjPatrolPointGetBounds(EditorObject *edObj, Vec3 min, Vec3 max)
{
	Mat4 pointMat;
	WorldPatrolPointProperties *pointProperties = wlePatrolPointFromHandle(edObj->obj, pointMat);

	// approximate bounds with AA box twice size of icons (doesn't need to be precise)
	if (pointProperties)
	{
		scaleVec3(wlePatrolEndMin, 2, min);
		scaleVec3(wlePatrolEndMax, 2, max);
		addVec3(pointMat[3], min, min);
		addVec3(pointMat[3], max, max);
	}
}

static void wleEdObjPatrolPointStartMove(EditorObject **selection)
{
	int i;

	for (i = 0; i < eaSize(&selection); i++)
	{
		WleEncObjSubHandle *handle = selection[i]->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);
		WorldPatrolPointProperties *point = wlePatrolPointFromHandle(handle, NULL);
		trackerSetInvisible(tracker, true);

		if (!tracker)
			continue;

		point->moving = 1;
		editState.drawGhosts = true;
	}
}

static void wleEdObjPatrolPointMoving(EditorObject **selection)
{
	int i;

	// update draw positions
	for (i = 0; i < eaSize(&selection); i++)
	{
		WleEncObjSubHandle *handle = selection[i]->obj;
		WorldPatrolPointProperties *point = wlePatrolPointFromHandle(handle, NULL);

		if (point)
		{
			if (editState.mode == EditPlaceObjects)
			{
				if (wleSnapToYGetIntersection(selection[i]->mat[3], point->draw_mat[3]))
					copyVec3(point->draw_mat[3], selection[i]->mat[3]);
			}
			else
				copyVec3(selection[i]->mat[3], point->draw_mat[3]);
		}
	}

	// update draw matrices
	for (i = 0; i < eaSize(&selection); i++)
	{
		WleEncObjSubHandle *handle = selection[i]->obj;
		WorldPatrolProperties *patrol = wlePatrolFromHandle(handle, NULL);
		GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);
		Mat4 patrolMat;
		if(!tracker)
			continue;
		if (patrol && eaSize(&patrol->patrol_points) > 0)
		{
			// update current point's matrix
			trackerGetMat(tracker, patrolMat);
			wlePatrolPointGetMat(patrol, handle->childIdx, patrolMat, patrol->patrol_points[handle->childIdx]->draw_mat, false, true, false);
		}
	}

	// refresh the attribute editor placement info, if necessary
	wleEdObjMovingUpdateAEPosRot(selection);
}

static void wleEdObjPatrolPointEndMove(EditorObject **selection)
{
	WleEncObjSubHandle **handles = NULL;
	Vec3 *vecs = NULL;
	int i;

	// we never need to perform a real move operation whenever the user is moving the pivot temporarily
	if (editState.mode != EditPlaceObjects &&
		(editState.gizmoMode == EditPivotTemp || (editState.gizmoMode == EditPivot && eaSize(&selection) > 1)))
	{
		editState.tempPivotModified = true;
		return;
	}
	// if moving the selected objects themselves
	else
	{
		vecs = calloc(eaSize(&selection), sizeof(Vec3));
		for (i = 0; i < eaSize(&selection); i++)
		{
			copyVec3(selection[i]->mat[3], vecs[i]);
			eaPush(&handles, selection[i]->obj);
		}
	}

	// re-render the invisible trackers
	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handles[i]->parentHandle);
		WorldPatrolPointProperties *point = wlePatrolPointFromHandle(handles[i], NULL);
		trackerSetInvisible(tracker, false);
		if (point)
			point->moving = 0;
	}

	EditUndoBeginGroup(edObjGetUndoStack());

	// duplicate points first if doing a move copy
	if (editState.inputData.moveCopyKey && editState.gizmoMode == EditActual)
	{
		EditorObject **newSelection;

		wlePatrolPointActionDuplicate(selection);
		newSelection = edObjSelectionGet(EDTYPE_PATROL_POINT);
		eaDestroy(&handles);
		for (i = 0; i < eaSize(&newSelection); i++)
			eaPush(&handles, newSelection[i]->obj);
	}

	// perform move operation; in case of failure, reset the matrix position
	if (!wleOpMovePatrolPoints(handles, vecs))
	{
		wleRefreshState();
		wleGizmoUpdateMatrix();
		wleAERefresh();
	}

	EditUndoEndGroup(edObjGetUndoStack());

	eaDestroy(&handles);
	free(vecs);

	editState.drawGhosts = false;
}

static void wlePatrolPointActionDelete(EditorObject **selection)
{
	if (wleCheckCmdDelete(NULL))
	{
		WleEncObjSubHandle **points = NULL;
		int i;

		for (i = 0; i < eaSize(&selection); i++)
			eaPush(&points, selection[i]->obj);
		wleOpDeletePatrolPoints(points);
		eaDestroy(&points);
	}
}

static void wlePatrolPointActionDuplicate(EditorObject **selection)
{
	if (wleCheckCmdDuplicate(NULL))
	{
		WleEncObjSubHandle **points = NULL;
		int i;

		for (i = 0; i < eaSize(&selection); i++)
			eaPush(&points, selection[i]->obj);
		wleOpDuplicatePatrolPoints(points);
		eaDestroy(&points);
	}
}

// encounter actor callbacks
static bool wleEdObjEncounterActorSelect(EditorObject *edObj)
{
	bool ret = wleEncObjSubHandleSelect(edObj);
	if (ret)
	{
		WorldActorProperties *actor = wleEncounterActorFromHandle(edObj->obj, NULL);
		if (actor)
			actor->selected = 1;
	}

	return ret;
}

static bool wleEdObjEncounterActorDeselect(EditorObject *edObj)
{
	bool ret = wleEncObjSubHandleDeselect(edObj);
	if (ret)
	{
		WorldActorProperties *actor = wleEncounterActorFromHandle(edObj->obj, NULL);
		if (actor)
			actor->selected = 0;
	}

	return ret;
}

static WorldEncounterProperties* wleEdObjEncounterGetProperties(const WleEncObjSubHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);
	GroupDef *def;
	if(!tracker)
		return NULL;
	def = tracker->def;
	if(!def)
		return NULL;
	return def->property_structs.encounter_properties;
}

static void wleEdObjEncounterActorDraw(EditorObject **selection)
{
	int i;
	static bool bActorIndicesRequested = false;

	if(!bActorIndicesRequested)
	{
		ServerCmd_encounter_RequestActorIndexUpdates();
		bActorIndicesRequested = true;
	}

	for (i = 0; i < eaSize(&selection); i++)
	{
		Mat4 actorMat = {0};
		WleEncObjSubHandle *subHandle = selection[i]->obj;
		WorldActorProperties *actor = wleEncounterActorFromHandle(subHandle, actorMat);

		if (actor)
		{
			WorldEncounterProperties *encounter = wleEdObjEncounterGetProperties(subHandle);
			bool disabled = wleIsActorDisabled(encounter, actor);
			if (actor->moving)
			{
				//Draw Original Position
				if(editState.inputData.moveCopyKey)
					wleEncounterActorDraw(actorMat, true, disabled);
				//Draw New Position
				wleEncounterActorDraw(actor->draw_mat, false, disabled);
			}
			else
			{
				wleEncounterActorDraw(actorMat, true, disabled);
			}
		}
	}
}

float wleEncounterActorCollide(Vec3 start, Vec3 end, Vec3 intersection, WleEncObjSubHandle **object)
{
	Mat4 camMat;
	float closestDist = -1, dist;
	int i, j;

	// shouldn't hit actors if they're hidden
	if (EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideEncounterActors", 0))
		return -1;

	// this is assumed, but we set it to NULL anyway
	if (object)
		*object = NULL;

	gfxGetActiveCameraMatrix(camMat);

	for (i = 0; i < eaSize(&editState.encounterTrackers); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(editState.encounterTrackers[i]);
		if (tracker && tracker->enc_obj && tracker->enc_obj->type == WL_ENC_ENCOUNTER)
		{
			WorldEncounter *encounter = (WorldEncounter*) tracker->enc_obj;
			Mat4 boxInvMat;
			Vec3 startLocal, endLocal, intersect, nextHit;

			for (j = 0; j < eaSize(&encounter->properties->eaActors); j++)
			{
				// transform ray into local coords
				invertMat4(encounter->properties->eaActors[j]->draw_mat, boxInvMat);
				mulVecMat4(start, boxInvMat, startLocal);
				mulVecMat4(end, boxInvMat, endLocal);

				// retransform intersection to world coords
				if (lineBoxCollision(startLocal, endLocal, wleActorMin, wleActorMax, intersect))
				{
					mulVecMat4(intersect, encounter->properties->eaActors[j]->draw_mat, nextHit);

					// check if closest to cam thus far
					dist = distance3(camMat[3], nextHit);
					if (closestDist == -1 || dist < closestDist)
					{
						closestDist = dist;
						if (intersection)
							copyVec3(nextHit, intersection);
						if (object)
						{
							if (*object)
								wleEncObjSubHandleDestroy(*object);
							*object = wleEncObjSubHandleCreate(editState.encounterTrackers[i], j); 
						}
					}
				}
			}
		}
	}

	return closestDist;
}

static float wleEdObjEncounterActorClickDetect(int mouseX, int mouseY, EditorObject ***edObjs)
{
	WleEncObjSubHandle *handle = NULL;
	Vec3 start, end;
	float closestDist;

	editLibCursorRay(start, end);
	closestDist = wleEncounterActorCollide(start, end, NULL, &handle);

	if (closestDist != -1 && handle)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);

		if (tracker && tracker->def && tracker->def->property_structs.encounter_properties && handle->childIdx >= 0 && handle->childIdx < eaSize(&tracker->def->property_structs.encounter_properties->eaActors))
			eaPush(edObjs, editorObjectCreate(handle, tracker->def->property_structs.encounter_properties->eaActors[handle->childIdx]->pcName, tracker->parent_layer, EDTYPE_ENCOUNTER_ACTOR));
	}

	return closestDist;
}

static void wleEdObjEncounterActorMarqueeDetect(int mouseX, int mouseY, int mouseX2, int mouseY2, int depth, Mat4 cam, Mat44 scrProjMat, EditorObject ***edObjs, bool crossingMode)
{
	WleFilter *filter = ui_ComboBoxGetSelectedObject(editorUIState->toolbarUI.marqueeFilterCombo);
	Vec3 marqueeMin, marqueeMax;
	Vec3 bottomLeft, topRight, camDir;
	Vec4_aligned boxVerts[8];
	int i, j, k;

	// can't select points if they're hidden
	if (EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideEncounterActors", 0))
		return;

	marqueeMin[0] = mouseX;
	marqueeMin[1] = mouseY;
	marqueeMin[2] = 0;
	marqueeMax[0] = mouseX2;
	marqueeMax[1] = mouseY2;
	marqueeMax[2] = depth;
	scaleVec3(cam[2], -1, camDir);

	for (i = 0; i < eaSize(&editState.encounterTrackers); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(editState.encounterTrackers[i]);
		if (tracker &&
			(tracker->open || (filter && filter->ignoreNodeState)) &&
			tracker->enc_obj && tracker->enc_obj->type == WL_ENC_ENCOUNTER)
		{
			WorldEncounter *encounter = (WorldEncounter*) tracker->enc_obj;
			for (j = 0; j < eaSize(&encounter->properties->eaActors); j++)
			{
				// project the actor's bounding box onto the screen and get the axis-aligned bounding box of the
				// projected vertices
				editLibFindScreenCoords(wleActorMin, wleActorMax, encounter->properties->eaActors[j]->draw_mat, scrProjMat, bottomLeft, topRight);
				mulBounds(wleActorMin, wleActorMax, encounter->properties->eaActors[j]->draw_mat, boxVerts);

				// we also calculate distances from each bounding vertex to the plane that intersects the
				// camera whose normal is the camera direction - this allows us to determine the "z" bound
				// so that objects that are behind the camera or too far in front won't get selected
				for (k = 0; k < 8; k++)
				{
					Vec3 vertDir;
					float distFromCamPlane;

					subVec3(boxVerts[k], cam[3], vertDir);
					distFromCamPlane = dotVec3(camDir, vertDir);
					if (!k || bottomLeft[2] > distFromCamPlane)
						bottomLeft[2] = distFromCamPlane;
					if (!k || topRight[2] < distFromCamPlane)
						topRight[2] = distFromCamPlane;
				}

				// if the current actor is bound by our marquee (in non-crossing mode) or touches our marquee (in crossing mode), then add it to the selection
				if ((crossingMode && boxBoxCollision(marqueeMin, marqueeMax, bottomLeft, topRight)) || (!crossingMode && wleBoxContainsAA(marqueeMin, marqueeMax, bottomLeft, topRight)))
				{
					WleEncObjSubHandle *handle = wleEncObjSubHandleCreate(trackerHandleFromTracker(tracker), j);

					if (handle)
					{
						WleFilter *marqueeFilter = ui_ComboBoxGetSelectedObject(editorUIState->toolbarUI.marqueeFilterCombo);
						EditorObject *obj;

						obj = editorObjectCreate(handle, encounter->properties->eaActors[j]->pcName, tracker->parent_layer, EDTYPE_ENCOUNTER_ACTOR);

						if (wleFilterApply(obj, marqueeFilter))
							eaPush(edObjs, obj);
						else
							editorObjectDeref(obj);
					}
				}
			}
		}
	}
}

static void wleEdObjEncounterActorGetMat(EditorObject *edObj, Mat4 mat)
{
	WleEncObjSubHandle *handle = edObj->obj;
	GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);
	WorldActorProperties *actor = NULL;
	Mat4 trackerMat;

	if (tracker && tracker->def && tracker->def->property_structs.encounter_properties && handle->childIdx >= 0 && handle->childIdx < eaSize(&tracker->def->property_structs.encounter_properties->eaActors))
		actor = tracker->def->property_structs.encounter_properties->eaActors[handle->childIdx];

	copyMat4(unitmat, mat);
	if (tracker && actor)
	{
		Mat4 actorMat;
		trackerGetMat(tracker, trackerMat);
		createMat3YPR(actorMat, actor->vRot);
		copyVec3(actor->vPos, actorMat[3]);
		mulMat4(trackerMat, actorMat, mat);
	}
}

static void wleEdObjEncounterActorGetBounds(EditorObject *edObj, Vec3 min, Vec3 max)
{
	Mat4 actorMat;
	WorldActorProperties *actorProperties = wleEncounterActorFromHandle(edObj->obj, actorMat);

	// approximate bounds with AA box twice size of icons (doesn't need to be precise)
	if (actorProperties)
	{
		scaleVec3(wleActorMin, 2, min);
		scaleVec3(wleActorMax, 2, max);
		addVec3(actorMat[3], min, min);
		addVec3(actorMat[3], max, max);
	}
}

static void wleEdObjEncounterActorStartMove(EditorObject **selection)
{
	int i;

	for (i = 0; i < eaSize(&selection); i++)
	{
		WleEncObjSubHandle *handle = selection[i]->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(handle->parentHandle);
		WorldActorProperties *actor = wleEncounterActorFromHandle(handle, NULL);
		trackerSetInvisible(tracker, true);

		if (!tracker)
			continue;

		copyMat4(selection[i]->mat, actor->draw_mat);
		actor->moving = 1;
		editState.drawGhosts = true;
	}
}

static void wleEdObjEncounterActorMoving(EditorObject **selection)
{
	int i;

	// update draw matrices
	for (i = 0; i < eaSize(&selection); i++)
	{
		WleEncObjSubHandle *handle = selection[i]->obj;
		WorldActorProperties *actor = wleEncounterActorFromHandle(handle, NULL);

		if (actor)
		{
			copyMat4(selection[i]->mat, actor->draw_mat);
			if (editState.mode == EditPlaceObjects)
			{
				if (wleSnapToYGetIntersection(selection[i]->mat[3], actor->draw_mat[3]))
					copyVec3(actor->draw_mat[3], selection[i]->mat[3]);
			}
		}
	}

	// refresh the attribute editor placement info, if necessary
	wleEdObjMovingUpdateAEPosRot(selection);
}

static void wleEdObjEncounterActorEndMove(EditorObject **selection)
{
	WleEncObjSubHandle **handles = NULL;
	Mat4 *mats = NULL;
	int i;

	// we never need to perform a real move operation whenever the user is moving the pivot temporarily
	if (editState.mode != EditPlaceObjects && 
		(editState.gizmoMode == EditPivotTemp || (editState.gizmoMode == EditPivot && eaSize(&selection) > 1)))
	{
		editState.tempPivotModified = true;
		return;
	}
	// if moving the selected objects themselves
	else
	{
		mats = calloc(eaSize(&selection), sizeof(Mat4));
		for (i = 0; i < eaSize(&selection); i++)
		{
			copyMat4(selection[i]->mat, mats[i]);
			eaPush(&handles, selection[i]->obj);
		}
	}

	// re-render the invisible trackers
	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handles[i]->parentHandle);
		WorldActorProperties *actor = wleEncounterActorFromHandle(handles[i], NULL);
		trackerSetInvisible(tracker, false);
		if (actor)
			actor->moving = 0;
	}

	EditUndoBeginGroup(edObjGetUndoStack());

	// duplicate actors first if doing a move copy
	if (editState.inputData.moveCopyKey && editState.gizmoMode == EditActual)
	{
		EditorObject **newSelection;

		wleEncounterActorActionDuplicate(selection);
		newSelection = edObjSelectionGet(EDTYPE_ENCOUNTER_ACTOR);
		eaDestroy(&handles);
		for (i = 0; i < eaSize(&newSelection); i++)
			eaPush(&handles, newSelection[i]->obj);
	}


	// perform move operation; in case of failure, reset the matrix position
	if (!wleOpMoveEncounterActors(handles, mats))
	{
		wleRefreshState();
		wleGizmoUpdateMatrix();
		wleAERefresh();
	}

	EditUndoEndGroup(edObjGetUndoStack());

	eaDestroy(&handles);
	free(mats);

	editState.drawGhosts = false;
}

static void wleEncounterActorActionDelete(EditorObject **selection)
{
	if (wleCheckCmdDelete(NULL))
	{
		WleEncObjSubHandle **actors = NULL;
		int i;

		for (i = 0; i < eaSize(&selection); i++)
			eaPush(&actors, selection[i]->obj);
		wleOpDeleteEncounterActors(actors);
		eaDestroy(&actors);
	}
}

static void wleEncounterActorActionDuplicate(EditorObject **selection)
{
	if (wleCheckCmdDuplicate(NULL))
	{
		WleEncObjSubHandle **actors = NULL;
		int i;

		for (i = 0; i < eaSize(&selection); i++)
			eaPush(&actors, selection[i]->obj);
		wleOpDuplicateEncounterActors(actors);
		eaDestroy(&actors);
	}
}

// logical group callbacks
static void wleLogicalGroupFree(EditorObject *edObj)
{
	SAFE_FREE(edObj->obj);
}

static U32 wleLogicalGroupCRC(EditorObject *edObj)
{
	cryptAdler32Init();
	cryptAdler32UpdateString((char*) edObj->obj);
	return cryptAdler32Final();	
}

static void wleLogicalGroupChildren(EditorObject *edObj, EditorObject ***children)
{
	WorldScope *zmapScope = (WorldScope*) zmapGetScope(NULL);
	WorldLogicalGroup *group;

	if (zmapScope && stashFindPointer(zmapScope->name_to_obj, edObj->obj, &group))
	{
		GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
		WorldScope *scope = scopeTracker ? scopeTracker->closest_scope : zmapScope;
		char *groupName;
		char *zmapScopeName;
		int i;

		assert(scope);
		for (i = 0; i < eaSize(&group->objects); i++)
		{
			if (!group->objects[i])
				continue;
			if (group->objects[i]->type == WL_ENC_LOGICAL_GROUP && scope->obj_to_name && stashFindPointer(scope->obj_to_name, group->objects[i], &groupName) && stashFindPointer(zmapScope->obj_to_name, group->objects[i], &zmapScopeName))
				eaPush(children, editorObjectCreate(strdup(zmapScopeName), groupName, group->objects[i]->layer, EDTYPE_LOGICAL_GROUP));
			else if (group->objects[i]->tracker && group->objects[i]->tracker->def)
				eaPush(children, editorObjectCreate(trackerHandleCreate(group->objects[i]->tracker), group->objects[i]->tracker->def->name_str, group->objects[i]->tracker->parent_layer, EDTYPE_TRACKER));
		}	
	}
}

static EditorObject *wleLogicalGroupParent(EditorObject *edObj)
{
	WorldScope *zmapScope = (WorldScope*) zmapGetScope(NULL);
	WorldLogicalGroup *group;

	if (zmapScope && stashFindPointer(zmapScope->name_to_obj, edObj->obj, &group))
	{
		WorldLogicalGroup *parentGroup = group->common_data.parent_group;

		if (parentGroup)
		{
			GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
			WorldScope *scope = scopeTracker ? scopeTracker->closest_scope : (WorldScope*) zmapGetScope(NULL);
			char *groupName;
			char *zmapScopeName;

			assert(scope);
			if (scope->obj_to_name && stashFindPointer(scope->obj_to_name, parentGroup, &groupName) && stashFindPointer(scope->obj_to_name, parentGroup, &zmapScopeName))
				return editorObjectCreate(strdup(zmapScopeName), groupName, parentGroup->common_data.layer, EDTYPE_LOGICAL_GROUP);
		}
	}

	return NULL;
}

static int wleLogicalGroupComp(EditorObject *edObj1, EditorObject *edObj2)
{
	return strcmp(edObj1->obj, edObj2->obj);
}

static bool wleLogicalGroupSelect(EditorObject *edObj)
{
	assert(edObj->type->objType == EDTYPE_LOGICAL_GROUP);

	if (!edObj->obj)
		return false;
	else if (editState.mode == EditSelectParent)
	{
		WorldScope *zmapScope = (WorldScope*) zmapGetScope(NULL);
		GroupTracker *scopeTracker = trackerFromTrackerHandle(editorUIState->trackerTreeUI.activeScopeTracker);
		WorldScope *closestScope = scopeTracker ? scopeTracker->closest_scope : zmapScope;
		WorldLogicalGroup *parentGroup;
		EditorObject **selection = NULL;
		char *parentScopeName = NULL;
	
		edObjSelectionGetAll(&selection);
		if (zmapScope && stashFindPointer(zmapScope->name_to_obj, edObj->obj, &parentGroup) && stashFindPointer(closestScope->obj_to_name, parentGroup, &parentScopeName))
		{
			const char **childScopeNames = NULL;
			int i;

			for (i = 0; i < eaSize(&selection); i++)
			{
				const char *uniqueName = NULL;
				if (selection[i]->type->objType == EDTYPE_TRACKER)
				{
					GroupTracker *tracker = trackerFromTrackerHandle(selection[i]->obj);
					if (tracker)
						uniqueName = trackerGetUniqueScopeName(scopeTracker ? scopeTracker->def : NULL, tracker, NULL);
				}
				else if (selection[i]->type->objType == EDTYPE_LOGICAL_GROUP)
				{
					WorldLogicalGroup *group;
					char *name;

					if (stashFindPointer(zmapScope->name_to_obj, selection[i]->obj, &group) && stashFindPointer(closestScope->obj_to_name, group, &name))
						uniqueName = name;
				}
				if (uniqueName)
					eaPush(&childScopeNames, uniqueName);
			}
			if (eaSize(&childScopeNames) > 0)
				wleOpAddToLogicalGroup(editorUIState->trackerTreeUI.activeScopeTracker, parentScopeName, childScopeNames, 0);
			eaDestroy(&childScopeNames);
		}
		eaDestroy(&selection);

		editState.mode = EditNormal;
		return false;
	}

	return true;
}

#endif
AUTO_RUN_EARLY;
void wleEdObjRegister(void)
{
#ifndef NO_EDITORS
	// dummy type used for the attribute editor
	editorObjectTypeRegister(EDTYPE_DUMMY, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	// trackers
	editorObjectTypeRegister(EDTYPE_TRACKER, wleEdObjTrackerCRC, wleEdObjTrackerFree, wleEdObjTrackerChildren, wleEdObjTrackerParent, wleEdObjTrackerSelectionDraw, wleEdObjTrackerClickDetect, wleEdObjTrackerMarqueeDetect, NULL, wleEdObjTrackerMovementEnable);
	edObjTypeSetSelectionCallbacks(EDTYPE_TRACKER, wleEdObjTrackerSelect, wleEdObjTrackerDeselect, wleEdObjTrackerSelectionChanged);
	edObjTypeSetCompCallback(EDTYPE_TRACKER, wleEdObjTrackerComp, wleEdObjTrackerComp);
	edObjTypeSetMovementCallbacks(EDTYPE_TRACKER, wleEdObjTrackerGetMat, wleEdObjTrackerStartMove, wleEdObjTrackerMoving, wleEdObjTrackerEndMove);
	edObjTypeSetMenuCallbacks(EDTYPE_TRACKER, wleTrackerContextMenuCreate);
	edObjTypeSetBoundsCallback(EDTYPE_TRACKER, wleEdObjTrackerGetBounds);

	edObjTypeActionRegister(EDTYPE_TRACKER, "lock_layer", wleEdObjLayerActionLock);
	edObjTypeActionRegister(EDTYPE_TRACKER, "revert_layer", wleEdObjLayerActionRevert);
	edObjTypeActionRegister(EDTYPE_TRACKER, "save_layer", wleEdObjLayerActionSave);
	edObjTypeActionRegister(EDTYPE_TRACKER, "save_and_close_layer", wleEdObjLayerActionSaveAndClose);

	edObjTypeActionRegister(EDTYPE_TRACKER, "delete", trackerActionDelete);
	edObjTypeActionRegister(EDTYPE_TRACKER, "duplicate", trackerActionDuplicate);
	edObjTypeActionRegister(EDTYPE_TRACKER, "edit_subobjects", trackerActionToggleSubobjects);

	// layers
	editorObjectTypeRegister(EDTYPE_LAYER, wleEdObjLayerCRC, NULL, wleEdObjLayerChildren, NULL, NULL, NULL, NULL, NULL, NULL);
	edObjTypeSetSelectionCallbacks(EDTYPE_LAYER, wleEdObjLayerSelect, NULL, wleEdObjLayerSelectionChanged);
	edObjTypeSetCompCallback(EDTYPE_LAYER, wleEdObjLayerCompare, wleEdObjLayerCompare);
	edObjTypeSetMenuCallbacks(EDTYPE_LAYER, wleUILayerContextMenu);

	edObjTypeActionRegister(EDTYPE_LAYER, "lock_layer", wleEdObjLayerActionLock);
	edObjTypeActionRegister(EDTYPE_LAYER, "revert_layer", wleEdObjLayerActionRevert);
	edObjTypeActionRegister(EDTYPE_LAYER, "save_layer", wleEdObjLayerActionSave);
	edObjTypeActionRegister(EDTYPE_LAYER, "save_and_close_layer", wleEdObjLayerActionSaveAndClose);

	// fx
	editorObjectTypeRegister(EDTYPE_FX_TARGET_NODE, NULL, NULL, NULL, wleEncObjFXTargetNodeParent, NULL, NULL, NULL, NULL, wleEdObjTrackerMovementEnable);
	edObjTypeSetCompCallback(EDTYPE_FX_TARGET_NODE, wleEdObjFXTargetNodeComp, wleEdObjFXTargetNodeComp);
	edObjTypeSetMovementCallbacks(EDTYPE_FX_TARGET_NODE, wleEdObjFXTargetNodeGetMat, NULL, wleEdObjFXTargetNodeMoving, wleEdObjFXTargetNodeEndMove);

	// encounter subselections
	editorObjectTypeRegister(EDTYPE_PATROL_POINT, wleEncObjSubHandleCRC, wleEncObjSubHandleEdObjDestroy, NULL, wleEncObjSubHandleParent, wleEdObjPatrolPointDraw, wleEdObjPatrolPointClickDetect, wleEdObjPatrolPointMarqueeDetect, NULL, wleEncObjSubHandleMovementEnable);
	edObjTypeSetSelectionCallbacks(EDTYPE_PATROL_POINT, wleEdObjPatrolPointSelect, wleEdObjPatrolPointDeselect, NULL);
	edObjTypeSetCompCallback(EDTYPE_PATROL_POINT, wleEncObjSubHandleComp, wleEncObjSubHandleComp);
	edObjTypeSetMovementCallbacks(EDTYPE_PATROL_POINT, wleEdObjPatrolPointGetMat, wleEdObjPatrolPointStartMove, wleEdObjPatrolPointMoving, wleEdObjPatrolPointEndMove);
	edObjTypeSetBoundsCallback(EDTYPE_PATROL_POINT, wleEdObjPatrolPointGetBounds);
	edObjTypeActionRegister(EDTYPE_PATROL_POINT, "delete", wlePatrolPointActionDelete);
	edObjTypeActionRegister(EDTYPE_PATROL_POINT, "duplicate", wlePatrolPointActionDuplicate);

	editorObjectTypeRegister(EDTYPE_ENCOUNTER_ACTOR, wleEncObjSubHandleCRC, wleEncObjSubHandleEdObjDestroy, NULL, wleEncObjSubHandleParent, wleEdObjEncounterActorDraw, wleEdObjEncounterActorClickDetect, wleEdObjEncounterActorMarqueeDetect, NULL, wleEncObjSubHandleMovementEnable);
	edObjTypeSetSelectionCallbacks(EDTYPE_ENCOUNTER_ACTOR, wleEdObjEncounterActorSelect, wleEdObjEncounterActorDeselect, NULL);
	edObjTypeSetCompCallback(EDTYPE_ENCOUNTER_ACTOR, wleEncObjSubHandleComp, wleEncObjSubHandleComp);
	edObjTypeSetMovementCallbacks(EDTYPE_ENCOUNTER_ACTOR, wleEdObjEncounterActorGetMat, wleEdObjEncounterActorStartMove, wleEdObjEncounterActorMoving, wleEdObjEncounterActorEndMove);
	edObjTypeSetBoundsCallback(EDTYPE_ENCOUNTER_ACTOR, wleEdObjEncounterActorGetBounds);
	edObjTypeActionRegister(EDTYPE_ENCOUNTER_ACTOR, "delete", wleEncounterActorActionDelete);
	edObjTypeActionRegister(EDTYPE_ENCOUNTER_ACTOR, "duplicate", wleEncounterActorActionDuplicate);

	// logical groups
	editorObjectTypeRegister(EDTYPE_LOGICAL_GROUP, wleLogicalGroupCRC, wleLogicalGroupFree, wleLogicalGroupChildren, wleLogicalGroupParent, NULL, NULL, NULL, NULL, NULL);
	edObjTypeSetCompCallback(EDTYPE_LOGICAL_GROUP, wleLogicalGroupComp, wleLogicalGroupComp);
	edObjTypeSetSelectionCallbacks(EDTYPE_LOGICAL_GROUP, wleLogicalGroupSelect, NULL, NULL);
#endif
}
#ifndef NO_EDITORS


/********************
* EDITOR MANAGER INTEGRATION
********************/
void wleSetDocSavedBit(void)
{
	EMEditorDoc *doc = wleGetWorldEditorDoc();
	if (doc)
	{
		int i;

		doc->saved = !zmapInfoGetUnsaved(NULL);
		doc->saved = doc->saved && !objectLibraryGetUnsaved();

		for (i = 0; doc->saved && i < zmapGetLayerCount(NULL); i++)
		{
			ZoneMapLayer *layer = zmapGetLayer(NULL, i);
			if (layer)
				doc->saved = doc->saved && !layerGetUnsaved(layer);
		}
	}
}

static EMEditorDoc *worldEditorNewDoc(const char *type, void *unused)
{
	EMEditorDoc *doc;
	char *s;

	PERFINFO_AUTO_START_FUNC();
	
	if (eaSize(&worldEditor.open_docs) > 0)
	{
		wleUINewZoneMapDialogCreate();
		return NULL;
	}
	else
	{
		// this is called to change maps
		assert(strcmpi(type, "zone") == 0);

		// TODO: tell server to change maps if necessary

		doc = calloc(sizeof(*doc), 1);
		strcpy(doc->doc_name, "worldmap");
		strcpy(doc->doc_type, type);
		s = strrchr(doc->doc_name, '.');
		if (s)
			*s = 0;

		strcpy(doc->doc_display_name, "World Editor");
		doc->saved = true;
		doc->edit_undo_stack = edObjGetUndoStack();

		// initialize other UI widgets
		wleUIInit(doc);

		PERFINFO_AUTO_STOP();

		return doc;
	}
}

static EMEditorDoc *worldEditorLoadDoc(const char *name, const char *type)
{
	return NULL;
}

static EMTaskStatus worldEditorSave(EMEditorDoc *doc)
{
	if (zmapIsSaving(NULL))
		return EM_TASK_INPROGRESS;
	if (zmapCheckFailedValidation(NULL))
		return EM_TASK_FAILED;
	wleCmdSave();
	return zmapIsSaving(NULL) ? EM_TASK_INPROGRESS : EM_TASK_SUCCEEDED;
}

static EMTaskStatus worldEditorSaveAs(EMEditorDoc *doc)
{
	wleCmdSaveAs();
	return EM_TASK_SUCCEEDED;
}

static EMTaskStatus worldEditorAutoSave(EMEditorDoc *doc)
{
	wleClientSave(true);
	return EM_TASK_SUCCEEDED;
}

static bool worldEditorCloseDocCheck(EMEditorDoc *doc, bool quitting)
{
	return quitting;
}

static void worldEditorCloseDoc(EMEditorDoc *doc)
{

	// TODO: add FAR more memory cleanup
	SAFE_FREE(doc);
}

static void wleTrackerDrawEncounter(GroupTracker *tracker, bool selected)
{
	Mat4 world_mat;
	WorldEncounterProperties *enc = tracker->def->property_structs.encounter_properties;
	F32 extraRad = 0;
	GfxVisualizationSettings *viz = gfxGetVisSettings();

	if(!viz->hide_aggro_volumes && (selected || g_DrawAllAggro))
	{
		trackerGetMat(tracker, world_mat);

		FOR_EACH_IN_EARRAY(enc->eaActors, WorldActorProperties, actor)
		{
			F32 len = lengthVec3(actor->vPos);

			if(len>extraRad)
				extraRad = len;
		}
		FOR_EACH_END;

		if(eaSize(&enc->eaActors))
		{
			F32 radius = gConf.iDefaultEditorAggroRadius+extraRad;
			Mat4 cam_mat;
			gfxDrawSphere3DARGB(world_mat[3], radius, 5, 0x60FF0000, 0);

			// "outline" the sphere
			gfxGetActiveCameraMatrix(cam_mat);
			gfxDrawCircle3DARGB(world_mat[3], cam_mat[2], cam_mat[1], 20, 0xFF000000, radius);
		}
	}
}

void wlePathNodeLinkSetSelected( int def1, int def2 )
{
	editState.selectedPathNodeLink.def_id1 = def1;
	editState.selectedPathNodeLink.def_id2 = def2;
}

static bool wlePathNodeLinkIsSelected( int def1, int def2 )
{
	return ((editState.selectedPathNodeLink.def_id1 == def1 && editState.selectedPathNodeLink.def_id2 == def2)
			|| (editState.selectedPathNodeLink.def_id1 == def2 && editState.selectedPathNodeLink.def_id2 == def1));
}

float g_PathNodeDrawDist = 500;
AUTO_CMD_FLOAT( g_PathNodeDrawDist, PathNodeDrawDist );

static void wleTrackerDrawPathNode( GroupTracker* tracker, StashTable stTrackersByDefID )
{
	Vec3 camPos;
	gfxGetActiveCameraPos( camPos );
	
	if( tracker->world_path_node && distance3( tracker->world_path_node->position, camPos ) < g_PathNodeDrawDist ) {
		Color color = {220, 180, 20, 255};
		Color selectedColor = { 120, 220, 20, 255};
		int j;

		for (j = 0; j < eaSize(&tracker->def->property_structs.path_node_properties->eaConnections); ++j)
		{
			WorldPathEdge *pEdge = tracker->def->property_structs.path_node_properties->eaConnections[j];
			if( pEdge ) {
				GroupTracker* otherTracker = NULL;
				if(   stashIntFindPointer( stTrackersByDefID, pEdge->uOther, &otherTracker ) && otherTracker) {
					gfxDrawLine3D(otherTracker->world_path_node->position, tracker->world_path_node->position,
								  wlePathNodeLinkIsSelected(tracker->def->name_uid, otherTracker->def->name_uid) ? selectedColor : color);
				}
			}
		}
	}
}

static void wleTrackerDraw(GroupTracker *tracker, bool selected, StashTable stTrackersByDefID)
{
	int i;
	if(!tracker)
		return;

	if(tracker->invisible)
		return;

	selected = selected || tracker->selected;

	if(tracker->def)
	{
		if(tracker->def->property_structs.encounter_properties)
			wleTrackerDrawEncounter(tracker, selected);

		if(tracker->def->property_structs.path_node_properties)
			wleTrackerDrawPathNode(tracker, stTrackersByDefID);
	}

	for(i=0; i<tracker->child_count; i++)
	{
		GroupTracker *child = tracker->children[i];

		wleTrackerDraw(child, selected, stTrackersByDefID);
	}
}

static void wleDrawTrackers(void)
{
	int i;

	for(i=0; i<zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		GroupTracker *tracker = layerGetTracker(layer);
		StashTable stLayerTrackersByDefID = stashTableCreateInt( 256 );

		groupTrackerBuildPathNodeTrackerTable( tracker, stLayerTrackersByDefID );
		wleTrackerDraw(tracker, false, stLayerTrackersByDefID);
		stashTableDestroy( stLayerTrackersByDefID );
	}
}

typedef struct GenesisZoneNodeLayout GenesisZoneNodeLayout;
void genesisDrawNodeLayout(GenesisZoneNodeLayout *layout);

void worldEditorDraw(EMEditorDoc *doc)
{
	GenesisZoneNodeLayout *layout = genesisGetLastNodeLayout();
	bool menuOpened = false;
	int width, height;
	int mouseX, mouseY;

	PERFINFO_AUTO_START_FUNC();

	// draw state messages
	gfxfont_SetFontEx(&g_font_Sans, 0, 1, 1, 0, 0x00FF00FF, 0x00FF00FF);
	gfxGetActiveDeviceSize(&width, &height);
	if (editState.mode == EditSelectParent)
		gfxfont_Printf(width / 2, 100, 10000, 1, 1, CENTER_X, "SELECT NEW PARENT");
	else if (editState.mode == EditPlaceObjects)
		gfxfont_Printf(width / 2, 100, 10000, 1, 1, CENTER_X, "PLACE OBJECT");
	if (editState.mode != EditPlaceObjects)
	{
		if (editState.gizmoMode == EditPivot)
			gfxfont_Printf(width / 2, 120, 10000, 1, 1, CENTER_X, "MOVING PIVOT");
		else if (editState.gizmoMode == EditPivotTemp)
			gfxfont_Printf(width / 2, 120, 10000, 1, 1, CENTER_X, "MOVING PIVOT (TEMP)");
	}
	// TomY TODO get some node drawing support

	if (layout)
		genesisDrawNodeLayout(layout);

	// notes
	wleNotesTick();

	// genesis
	wleGenesisUITick();

	// inputs
	if (editState.mode == EditPlaceObjects)
	{
		bool clickedOnUI = (mouseUnfilteredDown(MS_LEFT) && !mouseDown(MS_LEFT)) && !editState.quickPlaceState.objectLibraryClicked;
		editState.quickPlaceState.objectLibraryClicked = false;

		if (!editState.quickPlaceState.quickRotating)
		{
			if (mouseDown(MS_LEFT) && editState.quickPlaceState.timerIndex == 0)
			{
				editState.quickPlaceState.timerIndex = timerAlloc();
				timerStart(editState.quickPlaceState.timerIndex);
			}
			else if (mouseIsDown(MS_LEFT) && editState.quickPlaceState.timerIndex)
			{
				if (timerElapsed(editState.quickPlaceState.timerIndex) > 0.2)
				{
					editState.quickPlaceState.quickRotating = true;
					RotateGizmoSetMatrix(editState.quickPlaceState.quickRotateGizmo, editState.quickPlaceState.pivotMat);
					RotateGizmoActivateAxis(editState.quickPlaceState.quickRotateGizmo, edObjHarnessGetSnapNormal() ? edObjHarnessGetSnapNormalAxis() : 1);
				}
			}
		}
		if (mouseClickCoords(MS_LEFT, &mouseX, &mouseY) || clickedOnUI ||
			((editState.quickPlaceState.quickRotating || editState.quickPlaceState.timerIndex) && !mouseIsDown(MS_LEFT)))
		{
			TrackerHandle *parent;

			// by default, we create things into the default parent
			parent = trackerHandleCopy(editState.defaultParent);
			if (parent)
			{
				TrackerHandle **newHandles = NULL;
				EditorObject **newSubObjs = NULL;
				EditUndoStack *stack = edObjGetUndoStack();
				WleCopyBuffer ***bufferTemp = wleCopyBufferDup(*editState.quickPlaceState.buffer);
				Mat4 *mats = NULL;
				int maxCount = 0;
				int i;
				bool usingSelectionBuffer = false;

				wleChangeMode(EditNormal);
				dynArrayReserveStructs(mats, maxCount, 1); 
				EditUndoBeginGroup(stack);

				for (i = 0; i < eaSize(bufferTemp); i++)
				{
					WleCopyBuffer *buffer = (*bufferTemp)[i];

					// pasting/placing trackers
					if (buffer->pti == parse_WleTrackerBuffer)
					{
						WleTrackerBuffer *trackerBuffer = buffer->data;
						ZoneMapLayer *layer = zmapGetLayerByName(NULL, trackerBuffer->layerName);
						GroupDefLib *def_lib = layerGetGroupDefLib(layer);
						GroupDef *def;

 						if (def_lib)
							def = groupLibFindGroupDef(def_lib, trackerBuffer->uid, false);
						else
						{
							def = objectLibraryGetGroupDef(trackerBuffer->uid, true);
							if (!def)
								def = objectLibraryGetEditingGroupDef(trackerBuffer->uid, false);
						}
						if (def)
						{
							GroupDef **defs = NULL;
							eaPush(&defs, def);
							mulMat4(editState.quickPlaceState.pivotMat, buffer->relMat, mats[0]);
							wleOpCreateEx(parent, defs, mats, NULL, -1, &newHandles);
							eaDestroy(&defs);
						}
					}
					// pasting/placing patrol points
					else if (buffer->pti == parse_WlePatrolPointBuffer)
					{
						WlePatrolPointBuffer *pointBuffer = buffer->data;
						if (pointBuffer)
						{
							const TrackerHandle *parentHandle;
							int newIdx;

							mulMat4(editState.quickPlaceState.pivotMat, buffer->relMat, mats[0]);
							newIdx = wleOpAddPatrolPoint(pointBuffer->parentHandle, -1, mats[0][3]);
							parentHandle = pointBuffer->parentHandle;

							if (newIdx >= 0 && parentHandle)
							{
								EditorObject *newEdObj = wlePatrolPointEdObjCreate(parentHandle, newIdx);

								if (newEdObj)
									eaPush(&newSubObjs, newEdObj);
							}
						}
					}
					// pasting/placing encounter actors
					else if (buffer->pti == parse_WleEncounterActorBuffer)
					{
						WleEncounterActorBuffer *actorBuffer = buffer->data;
						if (actorBuffer)
						{
							const TrackerHandle *parentHandle;
							int newIdx;

							mulMat4(editState.quickPlaceState.pivotMat, buffer->relMat, mats[0]);
							newIdx = wleOpAddEncounterActor(actorBuffer->parentHandle, -1, mats[0]);
							parentHandle = actorBuffer->parentHandle;

							if (newIdx >= 0)
							{
								EditorObject *newEdObj = wleEncounterActorEdObjCreate(parentHandle, newIdx);
								
								if (newEdObj)
									eaPush(&newSubObjs, newEdObj);
							}
						}
					}
					else if (buffer->useCurrentSelection)
					{
						EditorObject **subSelection = NULL;
						TrackerHandle **selection = NULL;
						int j;

						wleSelectionGetTrackerHandles(&selection);
						for (j = 0; j < eaSize(&selection); j++)
							eaPush(&newHandles, trackerHandleCopy(selection[j]));
						eaDestroy(&selection);
						subSelection = edObjSelectionGet(EDTYPE_PATROL_POINT);
						for (j = 0; j < eaSize(&subSelection); j++)
							eaPush(&newSubObjs, subSelection[j]);
						subSelection = edObjSelectionGet(EDTYPE_ENCOUNTER_ACTOR);
						for (j = 0; j < eaSize(&subSelection); j++)
							eaPush(&newSubObjs, subSelection[j]);
						edObjHarnessDeactivate(editState.quickPlaceState.pivotMat, NULL);
						usingSelectionBuffer = true;
						break;
					}
				}

				wleTrackersSnapSubObjects(newHandles);
				eaForEach(&newSubObjs, editorObjectRef);

				if (editState.inputData.moveCopyKey && !usingSelectionBuffer)
				{
					wleTrackerDeselectAll();
					wleObjectPlaceEx(*bufferTemp, false);
				}
				else if (!editState.inputData.moveCopyKey)
				{
					wleTrackerDeselectAll();

					// select everything just placed (unless a duplication was done, in which
					// case the new objs will already be selected)
					wleTrackerSelectList(newHandles, true);
					edObjSelectList(newSubObjs, true, true);
				}
				EditUndoEndGroup(stack);
				wleCopyBufferFree(bufferTemp);
				SAFE_FREE(mats);
				eaDestroyEx(&newSubObjs, editorObjectDeref);
				eaDestroyEx(&newHandles, trackerHandleDestroy);
			}
			else
				emStatusPrintf("Cannot place object without a default parent.");
			trackerHandleDestroy(parent);

			inpHandled();
		}
		if (!mouseIsDown(MS_LEFT) || clickedOnUI)
		{
			editState.quickPlaceState.quickRotating = false;
			if (editState.quickPlaceState.timerIndex)
			{
				timerFree(editState.quickPlaceState.timerIndex);
				editState.quickPlaceState.timerIndex = 0;
			}
		}
	}

	wleGizmosOncePerFrame();

	// Do per-node draws
	wleDrawTrackers();

	// UI once-per-frame
	wleUIDraw();

	// Draw directional Compass
	wleUIDrawCompass();

	// check what's under the mouse and store the results
	wleRayCollideUpdate();

	// debugging
	wleDebugDraw();

	// progress bars
	progressOverlayDraw();

	PERFINFO_AUTO_STOP();
}

/******
* This function renders the ghost trackers when they are being modified without actually adding
* trackers to the tracker tree.
* PARAMS:
*   doc - EMEditorDoc
******/
static void worldEditorDrawGhosts(EMEditorDoc *doc)
{
	EditorObject **selection = edObjSelectionGet(EDTYPE_TRACKER);
	int i;

	// signal recalculation of frustum planes
	editState.planesCalculatedThisFrame = false;

	PERFINFO_AUTO_START_FUNC();
	if (editState.mode == EditPlaceObjects)
	{
		if (!editState.quickPlaceState.quickRotating && !editState.quickPlaceState.timerIndex)
		{
			if (editState.rayCollideInfo.results->hitSomething)
			{
				copyVec3(editState.rayCollideInfo.results->posWorldImpact, editState.quickPlaceState.pivotMat[3]);
				if (edObjHarnessGetSnapNormal())
					GizmoSnapToNormal(edObjHarnessGetSnapNormalAxis(), edObjHarnessGetSnapNormalInverse(), editState.quickPlaceState.pivotMat, editState.rayCollideInfo.results->normalWorld);
			}
			else
			{
				// draw a short distance in front of mouse
				Mat4 cam;
				int x, y;
				Vec3 start, end;

				mousePos(&x, &y);
				gfxGetActiveCameraMatrix(cam);
				editLibCursorSegment(cam, x, y, OBJ_PLACE_DIST, start, end);
				copyVec3(end, editState.quickPlaceState.pivotMat[3]);
			}
		}
		else if (editState.quickPlaceState.quickRotating)
		{
			RotateGizmoUpdate(editState.quickPlaceState.quickRotateGizmo);
			RotateGizmoDraw(editState.quickPlaceState.quickRotateGizmo);
			RotateGizmoGetMatrix(editState.quickPlaceState.quickRotateGizmo, editState.quickPlaceState.pivotMat);
		}

		// draw objects
		if (editState.quickPlaceState.buffer)
		{
			for (i = 0; i < eaSize(editState.quickPlaceState.buffer); i++)
			{
				WleCopyBuffer *buffer = (*editState.quickPlaceState.buffer)[i];
				Mat4 mat;

				if (buffer->pti == parse_WleTrackerBuffer)
				{
					WleTrackerBuffer *trackerBuffer = buffer->data;
					GroupDef *ghostDef = NULL;

					if (!trackerBuffer->layerName)
					{
						ghostDef = objectLibraryGetGroupDef(trackerBuffer->uid, true);
						if (!ghostDef)
							ghostDef = objectLibraryGetEditingGroupDef(trackerBuffer->uid, false);
					}
					else
					{
						ZoneMapLayer *layer = zmapGetLayerByName(NULL, trackerBuffer->layerName);
						GroupDefLib *def_lib = layerGetGroupDefLib(layer);
						if (def_lib)
							ghostDef = groupLibFindGroupDef(def_lib, trackerBuffer->uid, false);
					}
					if (ghostDef)
					{
						GroupTracker *tracker;
						TempGroupParams tgparams = {0};

						tgparams.no_culling = true;
						mulMat4(editState.quickPlaceState.pivotMat, buffer->relMat, mat);
						tracker = worldAddTempGroup(ghostDef, mat, &tgparams, true);
						if (tracker)
							wleEdObjTrackerDrawRecursive(tracker->def, tracker, true, !!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedWireframe", 1));
					}
				}
				else if (buffer->pti == parse_WlePatrolPointBuffer)
				{
					TempGroupParams tgparams = {0};

					tgparams.no_culling = true;
					mulMat4(editState.quickPlaceState.pivotMat, buffer->relMat, mat);
					worldAddTempGroup(editState.arrowDef, mat, &tgparams, true);
				}
				else if (buffer->pti == parse_WleEncounterActorBuffer)
				{
					TempGroupParams tgparams = {0};

					tgparams.no_culling = true;
					mulMat4(editState.quickPlaceState.pivotMat, buffer->relMat, mat);
					worldAddTempGroup(editState.actorDef, mat, &tgparams, true);
				}
				else if (buffer->useCurrentSelection)
				{
					edObjSelectionMoveToMat(editState.quickPlaceState.pivotMat);
				}
			}
		}
	}

	// draw patrols
	if (!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePatrolPoints", 0))
	{
		EditorObject **pointSelection = edObjSelectionGet(EDTYPE_PATROL_POINT);
		for (i = 0; i < eaSize(&editState.patrolTrackers); i++)
		{
			int j, k;
			GroupTracker *tracker = trackerFromTrackerHandle(editState.patrolTrackers[i]);
			GroupDef *def;
			WorldPatrolProperties *patrol_properties;
			Mat4 worldMat;
			bool being_moved = false;

			if(!tracker)
				continue;
			def = tracker->def;
			if(!def)
				continue;
			patrol_properties = def->property_structs.patrol_properties;
			if(!patrol_properties)
				continue;
			trackerGetMat(tracker, worldMat);

			for ( j=0; j < eaSize(&patrol_properties->patrol_points); j++ )
			{
				if(patrol_properties->patrol_points[j]->moving)
				{
					//ensure that this is the one that is actually selected
					for ( k=0; k < eaSize(&pointSelection); k++ )
					{
						WleEncObjSubHandle *handle = pointSelection[k]->obj;
						GroupTracker *selectedTracker = trackerFromTrackerHandle(handle->parentHandle);
						if(tracker == selectedTracker)
						{
							being_moved = true;
							break;
						}
					}
					break;
				}
			}
			if(being_moved)
				wlePatrolPathRefreshMat(patrol_properties, worldMat, false, true);

			wlePatrolPathDraw(patrol_properties, being_moved, false, worldMat);
		}
	}

	// draw encounters that have actors that are being moved
	if (!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideEncounterActors", 0))
	{
		EditorObject **actorSelection = edObjSelectionGet(EDTYPE_ENCOUNTER_ACTOR);
		for (i = 0; i < eaSize(&editState.encounterTrackers); i++)
		{
			int j, k;
			GroupTracker *tracker = trackerFromTrackerHandle(editState.encounterTrackers[i]);
			GroupDef *def;
			WorldEncounterProperties *encounter;
			bool being_moved = false;

			if(!tracker)
				continue;
			def = tracker->def;
			if(!def)
				continue;
			encounter = def->property_structs.encounter_properties;
			if(!encounter)
				continue;

			for (j = 0; j < eaSize(&encounter->eaActors); j++)
			{
				if (encounter->eaActors[j]->moving)
				{
					//ensure that this is the one that is actually selected
					for ( k=0; k < eaSize(&actorSelection); k++ )
					{
						WleEncObjSubHandle *handle = actorSelection[k]->obj;
						GroupTracker *selectedTracker = trackerFromTrackerHandle(handle->parentHandle);
						if(tracker == selectedTracker)
						{
							being_moved = true;
							break;
						}
					}
					break;
				}
			}

			if(being_moved)
			{
				Mat4 worldMat;
				trackerGetMat(tracker, worldMat);
				for (j = 0; j < eaSize(&encounter->eaActors); j++)
				{
					WorldActorProperties *actor = encounter->eaActors[j];
					// Only draw the ones not moving, because moving ones are handled elsewhere
					if (!actor->moving)
					{
						Mat4 tempMat, actorMat;
						createMat3YPR(tempMat, actor->vRot);
						copyVec3(actor->vPos, tempMat[3]);
						mulMat4(worldMat, tempMat, actorMat);
						wleEncounterActorDraw(actorMat, false, wleIsActorDisabled(encounter, actor));
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void worldEditorInitCallback(EMEditor *editor)
{
	GfxVisualizationSettings *visSettings = gfxGetVisSettings();
	Vec4 prefTintColor;

	copyMat4(unitmat, editState.quickPlaceState.pivotMat);

	// create editor state structs
	editorUIState = calloc(1, sizeof(*editorUIState));
	editorUIState->trackerTreeUI.currRefreshNode = &editorUIState->trackerTreeUI.topRefreshNode;
	editorUIState->showingLogicalTree = false;
	editorUIState->showingLogicalNames = false;

	// initialize harness
	edObjHarnessInit();

	// initialize UI
	wleMenuInitMenus();
	wleUIToolbarInit();
	wleUIRegisterInfoWinEntries(editor);

	// initialize options tabs
	wleOptionsRegisterTabs();

	// load preferences
	wleOptionsVolumesLoadPrefs();

	visSettings->hide_all_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideAllVols", 0);
	visSettings->hide_audio_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideAudioVols", 0);
	visSettings->hide_occlusion_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideOccVols", 0);
	visSettings->hide_skyfade_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideSkyVols", 0);
	visSettings->hide_neighborhood_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideNeighborhoodVols", 0);
	visSettings->hide_interaction_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideOptionalActionVols", 0);
	visSettings->hide_landmark_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideLandmarkVols", 0);
	visSettings->hide_power_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HidePowerVols", 0);
	visSettings->hide_warp_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideWarpVols", 0);
	visSettings->hide_genesis_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideGenesisVols", 0);
	visSettings->hide_aggro_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideAggroVols", 0);
	visSettings->hide_exclusion_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideExclusionVols", 0);
	visSettings->hide_untyped_volumes = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "HideUntypedVols", 0);

	worldSetSelectedWireframe(EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedGeo", 1),
		EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedTinted", 0));
	prefTintColor[0] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintR", 1.0f);
	prefTintColor[1] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintG", 0.0f);
	prefTintColor[2] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintB", 0.0f);
	prefTintColor[3] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintA", 1.0f);
	worldSetSelectedTintColor(prefTintColor);

	editorUIState->disableVolColl = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "DisableVolColl", 0);
	editorUIState->showHiddenLibs = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ShowHiddenLibs", 0);
	editState.replaceOnCreate = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ReplaceOnCreate", 1);
	editState.repeatCreateAcrossSelection = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "RepeatCreate", 1);
	
	// Make sure we will get edit copies of messages
	resSetDictionaryEditMode(gMessageDict, true);

	// Load the object library (if not loaded yet)
	objectLibraryLoad();

	// Load genesis/UGC data
	genesisLoadAllLibraries();
	ugcResourceInfoPopulateDictionary();

	// Get expression functions from the server
	exprEdInit();
}

static void worldEditorFocus(EMEditorDoc *doc)
{
	PERFINFO_AUTO_START_FUNC();
	worldEditorClientOncePerFrame(false);
	wleUITrackerTreeRefresh(NULL);

	// show the map desc
	{
		MapDescEditDoc *mapDescEd = GMDEmbeddedMapDesc();
		if (mapDescEd)
			ui_WindowShow(mapDescEd->pMainWindow);
	}

	PERFINFO_AUTO_STOP();
}

static void worldEditorLostFocus(EMEditorDoc *doc)
{
	PERFINFO_AUTO_START_FUNC();

	// hide the map desc
	{
		MapDescEditDoc *mapDescEd = GMDEmbeddedMapDesc();
		if (mapDescEd)
			ui_WindowHide(mapDescEd->pMainWindow);
	}

	PERFINFO_AUTO_STOP();
}

// NOTE: for now, there doesn't seem to be any intuitive reason to allow cutting/copying patrol points, so it
// has been disabled for now
static void worldEditorCopy(EMEditorDoc *doc, bool cut)
{
//	WleEncObjSubHandle **subhandles = NULL;
	TrackerHandle **handles = NULL;
	EditorObject **selection;
	WleCopyBuffer ***buffer;
	Mat4 invMat, mat, finalMat;
	int i;
	
	// validate state
	if (!wleCheckCmdCutCopyPaste(NULL))
		return;

	// handle attribute copying first
	if (wleAECopyAttributes() && !cut)
	{
		emStatusPrintf("Copied selected attributes.");
		return;
	}

	// validate trackers
	selection = edObjSelectionGet(EDTYPE_TRACKER);
	for (i = 0; i < eaSize(&selection); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle((TrackerHandle*) selection[i]->obj);
		if (tracker && !tracker->parent)
		{
			emStatusPrintf("Cannot copy layers.");
			return;
		}
	}

	// validate: SOMETHING is being cut/copied
	if (wleSelectionGetCount() == 0)
		return;

	edObjHarnessGetGizmoMatrix(mat);
	invertMat4(mat, invMat);
	buffer = calloc(1, sizeof(*buffer));

	// cut/copy trackers
	wleSelectionGetTrackerHandles(&handles);
	for (i = 0; i < eaSize(&handles); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handles[i]);
		if (tracker && tracker->def)
		{
			WleTrackerBuffer *trackerBuffer = StructCreate(parse_WleTrackerBuffer);
			if (!groupIsObjLib(tracker->def))
				trackerBuffer->layerName = StructAllocString(layerGetFilename(tracker->parent_layer));
			trackerBuffer->uid = tracker->def->name_uid;
			trackerGetMat(tracker, mat);
			mulMat4(invMat, mat, finalMat);
			wleCopyBufferAdd(buffer, trackerBuffer, parse_WleTrackerBuffer, finalMat);

			langMakeEditorCopy(parse_GroupProperties, &tracker->def->property_structs, true);
		}
	}
	if (cut && i && !wleOpDeleteEx(handles, false))
	{
		eaDestroy(&handles);
		wleCopyBufferFree(buffer);
		return;
	}
	eaDestroy(&handles);

	// cut/copy patrol points
	selection = edObjSelectionGet(EDTYPE_PATROL_POINT);
	if (eaSize(&selection) > 0)
		emStatusPrintf("Cannot %s selected patrol points.", cut ? "cut" : "copy");

	if (cut)
		emStatusPrintf("Cut selection.");
	else
		emStatusPrintf("Copied selection.");

	emAddToClipboardCustom("WorldEditorBuffer", wleCopyBufferFree, buffer);
}

static void worldEditorPaste(EMEditorDoc *doc, ParseTable *pti, const char *type, void *data)
{
	TrackerHandle **handles = NULL;

	// ensure we're pasting world editor contents
	if (!type || !wleCheckCmdCutCopyPaste(NULL))
		return;

	if (strcmpi(type, "WorldEditorBuffer") == 0)
	{
		WleCopyBuffer ***buffer = data;
		wleObjectPlaceEx(*buffer, false);
	}
	else if (strcmpi(type, "WorldEditorAttributeBuffer") == 0)
	{
		wleAEPasteAttributes(data);
		emStatusPrintf("Pasted attributes.");
	}
}

static void worldEditorReload(EMEditorDoc *doc)
{
	int i;
	const char *zmapFilename;

	if (!doc)
		return;

	zmapFilename = zmapGetFilename(NULL);

	if (zmapFilename) {
		int layer_cnt = zmapGetLayerCount(NULL);

		//Zone Map
		if(zmapLocked(NULL)) {
			if(fileIsReadOnly(zmapFilename)) {
				char modal_message[1024];
				sprintf(modal_message, "Zone Map was locked but now is not writable on disk. We are about to revert all layers and the zone map (%s)", zmapFilename);
				ui_ModalDialog( "Reverting Map", modal_message, ColorBlack, UIOk );
				globCmdParse("InitMap");
				return;
			}
		}

		//Layers
		for ( i=0; i < layer_cnt; i++ ) {
			ZoneMapLayer *layer = zmapGetLayer(NULL, i);
			if(layerGetLocked(layer)) {
				const char *layerFilename = layerGetFilename(layer);
				if(fileIsReadOnly(layerFilename)) {
					char modal_message[1024];
					sprintf(modal_message, "Layer was locked but now is not writable on disk. We are about to revert your changes. (%s)", layerFilename);
					ui_ModalDialog( "Reverting Layer", modal_message, ColorBlack, UIOk );
					wleUISetLayerModeEx(layer, LAYER_MODE_GROUPTREE, false, true);
				}
			}
		}
	}
}

static void editMapChangedCallback(void *unused, bool bUnused)
{
	PERFINFO_AUTO_START_FUNC();

	if (worldEditor.inited)
	{
		// refresh some states
		wleRefreshState();
		EditUndoStackClear(edObjGetUndoStack());
	}

	// do any new map stuff here
	worldEditorClientOncePerFrame(true);

	if (editorUIState)
	{
		wleUIObjectTreeRefresh();
		wleUIOncePerFrame();
	}
	wleUIMapLayersRefresh();

	PERFINFO_AUTO_STOP();
}

void worldEditorSetLink(NetLink **pLink)
{
	editState.link = pLink;
}

EMPicker* wleGetObjectPicker()
{
	return emPickerGetByName( "Object Picker" );
}

//////////////////////////////////////////////////////////////////////////

static ResourceGroup skyPickerTree;
static bool bSkyPickerRefreshRequested = false;

static bool skyPickerSelectedGlob(EMPicker *pPicker, EMPickerSelection *pSelection)
{
	assert(pSelection->table == parse_ResourceInfo);

	sprintf(pSelection->doc_name, "%s", ((ResourceInfo*)pSelection->data)->resourceName);
	strcpy(pSelection->doc_type, "SkyInfo");

	return true;
}

static void skyPickerRefreshPicker(void *data)
{
	resBuildGroupTree("SkyInfo", &skyPickerTree);
	emPickerRefresh(&skyPicker);
	bSkyPickerRefreshRequested = false;
}

static void skyPickerReferenceCallback(enumResourceEventType eType, const char *pDictName, const void *pData2, void *pData1, void *pUserData)
{
	if (!bSkyPickerRefreshRequested)
	{	
		bSkyPickerRefreshRequested = true;
		emQueueFunctionCall(skyPickerRefreshPicker, NULL);
	}
}

static void skyPickerEnter(EMPicker *pPicker)
{
	// Detect changes
	resDictRegisterEventCallback("SkyInfo", skyPickerReferenceCallback, NULL);

	// Load the list
	resBuildGroupTree("SkyInfo", &skyPickerTree);
	emPickerRefresh(&skyPicker);
}

static void skyPickerLeave(EMPicker *pPicker)
{
	resDictRemoveEventCallback("SkyInfo", skyPickerReferenceCallback);
}

static void skyPickerInit(EMPicker *pPicker)
{
	EMPickerDisplayType *pDispType;

	pPicker->display_data_root = &skyPickerTree;
	pPicker->display_parse_info_root = parse_ResourceGroup;

	pDispType = calloc(1, sizeof(EMPickerDisplayType));
	pDispType->parse_info = parse_ResourceGroup;
	pDispType->display_name_parse_field = "Name";
	pDispType->color = CreateColorRGB(0, 0, 0);
	pDispType->selected_color = CreateColorRGB(255, 255, 255);
	pDispType->is_leaf = 0;
	eaPush(&pPicker->display_types, pDispType);

	pDispType = calloc(1, sizeof(EMPickerDisplayType));
	pDispType->parse_info = parse_ResourceInfo;
	pDispType->display_name_parse_field = "resourceName";
	pDispType->display_notes_parse_field = "resourceNotes";
	pDispType->selected_func = skyPickerSelectedGlob;
	pDispType->color = CreateColorRGB(0,0,80);
	pDispType->selected_color = CreateColorRGB(255, 255, 255);
	pDispType->is_leaf = 1;
	eaPush(&pPicker->display_types, pDispType);
}

#endif
AUTO_RUN;
void setupSkyPicker(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return;

	// sky picker registration
	skyPicker.allow_outsource = 1;
	strcpy(skyPicker.picker_name, "Sky Picker");
	skyPicker.init_func = skyPickerInit;
	skyPicker.enter_func = skyPickerEnter;
	skyPicker.leave_func = skyPickerLeave;
	strcpy(skyPicker.default_type, "SkyInfo");
	emPickerRegister(&skyPicker);
#endif
}
#ifndef NO_EDITORS

//////////////////////////////////////////////////////////////////////////
//
bool wleZoneMapLockCallback(EMEditor *editor, const char *name, void *state_data, EMResourceState state, void *callback_data, bool success);
bool wleZoneMapSaveCallback(EMEditor *editor, const char *name, void *state_data, EMResourceState state, void *callback_data, bool success);


void worldEditorInit(int editCmd)
{
	strcpy(worldEditor.editor_name, "World Editor");
	worldEditor.allow_save = 1;

	worldEditor.init_func = worldEditorInitCallback;
	worldEditor.new_func = worldEditorNewDoc;
	worldEditor.load_func = worldEditorLoadDoc;
	worldEditor.close_check_func = worldEditorCloseDocCheck;
	worldEditor.close_func = worldEditorCloseDoc;
	worldEditor.custom_save_func = worldEditorSave;
	worldEditor.save_as_func = worldEditorSaveAs;
	worldEditor.autosave_func = worldEditorAutoSave;
	worldEditor.autosave_interval = 10;
	worldEditor.draw_func = worldEditorDraw;
	worldEditor.ghost_draw_func = worldEditorDrawGhosts;
	worldEditor.got_focus_func = worldEditorFocus;
	worldEditor.lost_focus_func = worldEditorLostFocus;
	worldEditor.copy_func = worldEditorCopy;
	worldEditor.paste_func = worldEditorPaste;
	worldEditor.reload_func = worldEditorReload;

	worldEditor.keybinds_name = "GrouptreeEditor";
	worldEditor.keybind_version = 4;
	worldEditor.default_type = "zone";
	strcpy(worldEditor.default_workspace, "Environment Editors");
	worldEditor.disable_auto_checkout = 1;
	worldEditor.primary_editor = 1;
	worldEditor.allow_external_docs = 1;
	worldEditor.force_reload = 1;

	emRegisterEditor(&worldEditor);
	emRegisterFileType("zone", NULL, "World Editor");
	emAddMapChangeCallback(editMapChangedCallback, NULL);

	emAddDictionaryStateChangeHandler(&worldEditor, "ZoneMap", NULL, wleZoneMapLockCallback, wleZoneMapSaveCallback, NULL, NULL);

	// editState initialization
	wleGizmoInit();
	editState.editCmd = editCmd;
	editState.transformMode = EditTranslateGizmo;
	editState.gizmoMode = EditActual;
	editState.rayCollideInfo.results = calloc(1, sizeof(*editState.rayCollideInfo.results));
	editState.rayCollideInfo.results->hitSomething = 0;
	editState.lockedSelection = false;
	editState.editOriginal = false;
	editState.lastWorldTime = -1;
	editState.lastObjLibTime = objectLibraryLastUpdated();
	editState.tempPivotModified = false;
	editState.drawGhosts = false;
	editState.replaceOnCreate = true;
	editState.repeatCreateAcrossSelection = true;
	editState.hiddenTrackers = NULL;
	editState.frozenTrackers = NULL;
}


/******
* This is used to update the contents of the edit state's ray collision results to determine what is under
* the mouse pointer.
******/
void wleRayCollideUpdate(void)
{
	Vec3 start, end;
	U32 filterBits = WC_FILTER_BIT_EDITOR | WC_FILTER_BIT_TERRAIN;
	if (!SAFE_MEMBER(editorUIState, disableVolColl))
		filterBits |= WC_FILTER_BIT_VOLUME;	

	editLibCursorRay(start, end);
	worldCollideRay(PARTITION_CLIENT, start, end, filterBits, editState.rayCollideInfo.results);

	editState.rayCollideInfo.entry = NULL;
	editState.rayCollideInfo.volumeEntry = NULL;
	editState.rayCollideInfo.model = NULL;
	editState.rayCollideInfo.heightMap = NULL;
	editState.rayCollideInfo.mat = NULL;
	editState.rayCollideInfo.physProp = NULL;
	if (editState.rayCollideInfo.results->hitSomething)
	{
		WorldCollObject* wco = editState.rayCollideInfo.results->wco;
		S32 triIndex = editState.rayCollideInfo.results->tri.index;

		
		editState.rayCollideInfo.physProp = wcoGetPhysicalProperties(wco, triIndex, editState.rayCollideInfo.results->posWorldImpact, &editState.rayCollideInfo.mat);

		if(wcoGetUserPointer(	wco,
								entryCollObjectMsgHandler,
								&editState.rayCollideInfo.entry))
		{
			editState.rayCollideInfo.model = SAFE_MEMBER(editState.rayCollideInfo.entry, model);
		}
		else if(wcoGetUserPointer(	wco,
									heightMapCollObjectMsgHandler,
									&editState.rayCollideInfo.heightMap))
		{
			if (editState.rayCollideInfo.heightMap)
				editState.rayCollideInfo.mat = terrainGetMaterial(editState.rayCollideInfo.heightMap, editState.rayCollideInfo.results->posWorldImpact[0], editState.rayCollideInfo.results->posWorldImpact[2]);
		}
		else if(wcoGetUserPointer(	wco,
									volumeCollObjectMsgHandler,
									&editState.rayCollideInfo.volumeEntry))
		{
			// Don't do anything.
		}
	}
}

void wleRayCollideClear(void)
{
	editState.rayCollideInfo.entry = NULL;
	editState.rayCollideInfo.volumeEntry = NULL;
	editState.rayCollideInfo.model = NULL;
	editState.rayCollideInfo.heightMap = NULL;
	editState.rayCollideInfo.mat = NULL;
	editState.rayCollideInfo.physProp = NULL;
}

AUTO_RUN;
void wleSetWorldLibCallbacks(void)
{
	wl_state.wle_is_actor_disabled_func = wleIsActorDisabled;
	wl_state.wle_patrol_point_get_mat_func = wlePatrolPointGetMat;
}

/******
* This function will generally handle all synchronization tasks in response to server updates.
* These need to be done before UI rendering is done, so we do this in EditLib's editLibOncePerFrame
* function instead of the Asset Manager's draw function.  This function will do things like
* ensuring the tracker tree is up-to-date and deleting invalid tracker handles, etc.
******/
// TODO: go through this function and refine it to handle reloading better
void worldEditorClientOncePerFrame(bool forceRefresh)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	// Refresh trackers if requested some number of frames ago
	if(editState.trackerRefreshRequested)
	{
		editState.trackerRefreshRequested = false;
		editState.trackerRefreshFrames = 10;
	}
	if(editState.trackerRefreshFrames == 1)
	{
		zmapTrackerUpdate(NULL, true, false);
		wleOpRefreshUI();
		editState.trackerRefreshFrames = 0;
	} 
	else if (editState.trackerRefreshFrames > 1)
	{
		editState.trackerRefreshFrames--;
	}

	wleUIOncePerFrame();
	if (!editState.link || !worldEditor.open_docs || eaSize(&worldEditor.open_docs) == 0)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (forceRefresh || objectLibraryLastUpdated() > editState.lastObjLibTime)
	{
		// refresh the object library if it changed
		wleUIObjectTreeRefresh();
		editState.lastObjLibTime = objectLibraryLastUpdated();
	}

	// detect any hiding of map layers from Editor Manager and synchronize
	PERFINFO_AUTO_START("sync layer hiding", 1);
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		GroupTracker *tracker = layerGetTracker(layer);
		TrackerHandle *handle;
		bool hidden;

		if (!tracker)
			continue;
		handle = trackerHandleFromTracker(tracker);
		hidden = wleTrackerIsHidden(handle);
		if (tracker->invisible && !hidden)
			wleTrackerHide(handle);
		else if (!tracker->invisible && hidden)
			wleTrackerUnhide(handle);
	}
	PERFINFO_AUTO_STOP();

	if (forceRefresh || worldGetModTime() > (U32)editState.lastWorldTime ||
		zmapInfoGetModTime(NULL) != editState.lastZmapTime)
	{
		EMEditorDoc *doc = wleGetWorldEditorDoc();
		DisplayMessage *zmapMessage = zmapInfoGetDisplayNameMessage(NULL);
		GroupTracker **allTrackers = NULL;
		const char *messageHandle;
		char *s;

		wleUILockMngrRefresh();
		wleSetDocSavedBit();

		// remove invalid trackers from selection
		wleRefreshState();
		wleUISearchClearUniqueness();
		wleGizmoUpdateMatrix();

		// update the stored mod time for determining whether an update occurred
		editState.lastWorldTime = worldGetModTime();
		editState.lastZmapTime = zmapInfoGetModTime(NULL);

		// update the editor tab for file name
		strcpy(doc->doc_name, zmapGetFilename(NULL));
		strcpy(doc->doc_type, "zone");
		s = strrchr(doc->doc_name, '.');
		if (s)
			*s = 0;

		doc->name_changed = 1;
		ui_LabelSetText(editorUIState->mapPropertiesUI.mapPathLabel, doc->doc_name);

		// request resource from server
		messageHandle = REF_STRING_FROM_HANDLE(zmapMessage->hMessage);
		if (!resIsEditingVersionAvailable(gMessageDict, messageHandle))
			resRequestOpenResource(gMessageDict, messageHandle);
		
		// update UI components
		PERFINFO_AUTO_START("update UI", 1);
		wleUITrackerTreeRefresh(NULL);
		wleUIMapLayersRefresh();
		wleAERefresh();
		wleUIFileListRefresh();
		wleUIMapPropertiesRefresh();
		wleUIVariablePropertiesRefresh();
		wleUIGlobalGAELayersRefresh();
		wleUIRegionMngrSettingsInit();
		wleUIRegionMngrRefresh();
		wleUIMiscPropertiesRefresh();
		wleToolbarUpdateEditingScratchLayer(NULL);

		// refresh draw groups
		editState.arrowDef = objectLibraryGetGroupDefByName("core_icons_patrol", true);
		editState.actorDef = objectLibraryGetGroupDefByName("core_icons_humanoid", true);
		editState.actorDisabledDef = objectLibraryGetGroupDefByName("core_icons_humanoid_disabled", true);
		editState.pointDef = objectLibraryGetGroupDefByName("core_icons_X", true);

		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_FUNC();
}

/*
Function converts EditorObject into a WrlDef.  Currently only keeps track of the object's position.
*/
static WrlDef* ConvertEditorObjToWrlDef(EditorObject* edObj) {
	WrlDef	*wrlDef = NULL;
	Mat4	objMatrix;

	wrlDef = Wrl_CreateDef(WRL_TRANSFORM);
	wrlDef->name = allocAddString(edObj->name);
	edObjGetMatrix(edObj, objMatrix);
	wrlDef->position[0] = objMatrix[3][0];
	wrlDef->position[1] = objMatrix[3][1];
	wrlDef->position[2] = objMatrix[3][2];
	wrlDef->pivot[0] = wrlDef->position[0];
	wrlDef->pivot[1] = wrlDef->position[1];
	wrlDef->pivot[2] = wrlDef->position[2];
	return wrlDef;
}

/*
Function runs through the list of selected objects and creates a .wrl file listing each object with their respective positions.
*/
static EMTaskStatus wleExportSelectionPositionDummies()
{
	EditorObject	**selectionObjs = NULL;
	WrlScene		*wrlScene = NULL;
	WrlDef			*wrlDef = NULL;
	U32				selectionCount;
	U32				selectionIndex;
	char			saveFile[ MAX_PATH ];
	char			saveDir[ MAX_PATH ];
	char			fullPath[ MAX_PATH ];

	wrlScene = StructCreate(parse_WrlScene);
	assert(wrlScene);
	edObjSelectionGetAll(&selectionObjs);
	selectionCount = eaSize(&selectionObjs);
	if (selectionCount == 0) {
		return EM_TASK_SUCCEEDED;
	}
	for (selectionIndex = 0; selectionIndex < selectionCount; selectionIndex++) {
		wrlDef = ConvertEditorObjToWrlDef(selectionObjs[selectionIndex]);
		eaPush(&wrlScene->wrlDefs, wrlDef);
	}

	if( UIOk != ui_ModalFileBrowser( "Save WRL As",
		"Save", UIBrowseNew, UIBrowseFiles, false,
		"Export/WRL",
		"Export/WRL",
		".wrl",
		SAFESTR( saveDir ), SAFESTR( saveFile ), NULL))
	{
			return EM_TASK_FAILED;
	}
	sprintf( fullPath, "%s/%s", saveDir, saveFile );
	ParserWriteTextFile(fullPath,parse_WrlScene,wrlScene,0,0);

	StructDestroy(parse_WrlScene, wrlScene);
	eaDestroy(&selectionObjs);
	return EM_TASK_SUCCEEDED;
}

AUTO_COMMAND;
void wleCmdExpPositionDummies(void) {
	wleExportSelectionPositionDummies();
}


static void wleExportSelectedToVrmlSub(GroupTracker *tracker, Mat4 mat)
{
	if (tracker)
	{
		int i;
		trackerOpen(tracker);
		if (tracker->def)
		{
			Model *model = tracker->def->model;
			if (model)
				vrmlDumpSub(model, mat, tracker->def->model_scale, tracker->def->hasTint0?tracker->def->tint_color0:unitvec3, tracker);
			assert(tracker->child_count == eaSize(&tracker->def->children));
			for (i=0; i<eaSize(&tracker->def->children); i++)
			{
				wleExportSelectedToVrmlSub(tracker->children[i], tracker->def->children[i]->mat);
			}
		}
	}
}
									   
AUTO_COMMAND;
void wleExportSelectedToVrml(void)
{
	EditorObject	**selectionObjs = NULL;
	U32				selectionCount;
	U32				selectionIndex;
	char			saveFile[ MAX_PATH ];
	char			saveDir[ MAX_PATH ];
	char			fullPath[ MAX_PATH ];

	edObjSelectionGetAll(&selectionObjs);
	selectionCount = eaSize(&selectionObjs);
	if (selectionCount == 0) {
		return;
	}
	if (UIOk != ui_ModalFileBrowser( "Save WRL As",
		"Save", UIBrowseNew, UIBrowseFiles, false,
		"Export/WRL",
		"Export/WRL",
		".wrl",
		SAFESTR( saveDir ), SAFESTR( saveFile ), NULL))
	{
			return;
	}
	sprintf( fullPath, "%s/%s", saveDir, saveFile );

	if (vrmlDumpStart(fullPath))
	{
		for (selectionIndex = 0; selectionIndex < selectionCount; selectionIndex++)
		{
			EditorObject *edObj = selectionObjs[selectionIndex];
			if (edObj)
			{
				TrackerHandle *handle;
				GroupTracker *tracker;

				assert(edObj->type->objType == EDTYPE_TRACKER);
				handle = edObj->obj;
				tracker = trackerFromTrackerHandle(handle);
				wleExportSelectedToVrmlSub(tracker, edObj->mat);
			}
		}

		vrmlDumpFinish();
	}

	eaDestroy(&selectionObjs);
}

bool wleCheckCmdSnapDown(UserData unused)
{
	return wleCheckDefault(NULL);
}

// Snap selected objects downward to terrain or geo it hits
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Editor.SnapDown");
void wleCmdSnapDown(void)
{
	if (wleCheckCmdSnapDown(NULL))
	{
		EditUndoStack *stack = edObjGetUndoStack();
		EditorObject **list = edObjSelectionGet(EDTYPE_TRACKER);

		EditUndoBeginGroup(stack);

		FOR_EACH_IN_EARRAY(list, EditorObject, object)
		{
			TrackerHandle *handle = object->obj;
			if (handle)
				wleOpSnapDown(handle);
		}
		FOR_EACH_END;

		EditUndoEndGroup(stack);
	}
}

#endif

#include "WorldEditorClientMain_h_ast.c"
#include "WorldEditorClientMain_c_ast.c"
