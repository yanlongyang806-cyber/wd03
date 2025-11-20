#define GENESIS_ALLOW_OLD_HEADERS
#ifndef NO_EDITORS

#include "WorldLib.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUI.h"
#include "WorldEditorUtil.h"
#include "WorldEditorNotes.h"
#include "WorldEditor.h"
#include "wlEncounter.h"
#include "wlGenesis.h"
#include "wlGenesisInterior.h"
#include "wlState.h"
#include "CurveEditor.h"
#include "TerrainEditor.h"
#include "Genesis.h"
#include "EditorServerMain.h"
#include "groupdbmodify.h"
#include "GfxEditorIncludes.h"
#include "cmdparse.h"
#include "net/net.h"
#include "StringCache.h"
#include "MapDescription.h"
#include "windefinclude.h"
#include "Genesis.h"
#include "wlEditorIncludes.h"
#include "fileutil2.h"
#include "Color.h"
#include "StringUtil.h"
#include "partition_enums.h"
#include "wlTerrainSource.h"

#include "WorldEditorOperations_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/******
* This file consists of the various atomic operations that will be called from the world editor.
* These operations are specifically those that modify the data being edited.  Therefore, these
* operations will not consist of actions performed in the editor that affect editing tools, gizmo
* states, UI interaction, etc.  As a guideline, each operation here should also take care of all
* of the following (when applicable):
*   - validation (including lock validation)
*   - reporting (of validation errors and of successful completion)
*   - refreshing/updating appropriate UI elements
*   - ensuring tracker tree (and data such as bounds) are up-to-date after the operation has completed
*****/

/********************
* UTIL
********************/
/******
* This function recursively gathers the unique names of all named trackers within the specified root tracker.
* PARAMS:
*   rootTracker - GroupTracker whose descendants' names (including itself) are to be compiled
*   scopeDef - GroupDef scope where the names are to be retrieved
*   uniqueNames - EArray of string names where the unique names are stored
******/
static void wleOpGetUniqueNamesDescend(GroupTracker *rootTracker, GroupDef *scopeDef, char ***uniqueNames)
{
	int i;

	if (groupDefNeedsUniqueName(rootTracker->def))
	{
		const char *uniqueName = trackerGetUniqueScopeName(scopeDef, rootTracker, NULL);
		if (uniqueName && uniqueName[0])
			eaPush(uniqueNames, strdup(uniqueName));
		else
			eaPush(uniqueNames, strdup(""));
	}

	for (i = 0; i < rootTracker->child_count; i++)
		wleOpGetUniqueNamesDescend(rootTracker->children[i], scopeDef, uniqueNames);
}

/******
* This function is the recursive workhorse for wleOpGetUniqueNames.
* PARAMS:
*   rootTracker - GroupTracker indicating current target of recursion; examined for a match with the baseDef
*   scopeDef - GroupDef where names are to be retrieved
*	baseDef - GroupDef to look for in the recursion
*   uidPath - int EArray indicating uid path from trackers matching the baseDef to the tracker from which names will be retrieved
*   uniqueNames - EArray used to store the names of affected trackers
******/
static void wleOpGetUniqueNamesRecurse(GroupTracker *rootTracker, GroupDef *scopeDef, GroupDef *baseDef, int *uidPath, char ***uniqueNames)
{
	if (!rootTracker || !scopeDef || !baseDef || !uniqueNames)
		return;
	else if (rootTracker->def == baseDef)
	{
		GroupTracker *trackerCopy = rootTracker;
		int i, j;

		// find corresponding tracker using uid path
		for (i = 0; i < eaiSize(&uidPath); i++)
		{
			int origCount = trackerCopy->child_count;

			for (j = 0; j < trackerCopy->child_count; j++)
			{
				if (trackerCopy->children[j]->uid_in_parent == (U32)uidPath[i])
				{
					trackerCopy = trackerCopy->children[j];
					break;
				}
			}

			// return if function fails to find the corresponding tracker
			if (j >= origCount)
				return;
		}

		wleOpGetUniqueNamesDescend(trackerCopy, scopeDef, uniqueNames);
	}
	else
	{
		int i;

		for (i = 0; i < rootTracker->child_count; i++)
			wleOpGetUniqueNamesRecurse(rootTracker->children[i], scopeDef, baseDef, uidPath, uniqueNames);
	}
}

/******
* This function recursively compiles the unique names of all trackers (and their named descendants) that match the specified
* tracker in the group tree with respect to a base tracker.  More precisely, the function takes in a tracker that is being
* moved in the grouptree and stores its unique name (and those of its descendants) so those unique names can be re-set to the
* same values after the grouptree rearrangement.  The function looks for all instances of the specified baseDef and locates all
* trackers that correspond to the specified tracker with respect to each instance of the baseDef.
* PARAMS:
*   baseTracker - GroupTracker for which to start looking for duplicates
*   tracker - GroupTracker that must be a descendant of baseTracker; function will compile all names of
*             tracker and its descendants with respect to baseTracker (i.e. for each instance of baseTracker's
*             GroupDef, the function will find each GroupTracker that corresponds to tracker and compile the unique
*             names)
*   uniqueNames - EArray used to store the names of affected trackers
******/
static void wleOpGetUniqueNames(GroupTracker *baseTracker, GroupTracker *tracker, char ***uniqueNames)
{
	GroupTracker *trackerCopy = tracker;
	GroupTracker *scopeTracker;
	int *uids = NULL;

	if (!baseTracker)
		baseTracker = layerGetTracker(tracker->parent_layer);
	if (!baseTracker)
		return;

	// compile unique uid relative path from tracker to baseTracker
	while (trackerCopy != baseTracker)
	{
		eaiPush(&uids, trackerCopy->uid_in_parent);
		trackerCopy = trackerCopy->parent;
	}
	eaiReverse(&uids);
	
	// ensure that tracker was a descendant of baseTracker
	assert(trackerCopy);

	// get closest scope to be modified
	scopeTracker = baseTracker->closest_scope->tracker;
	if (!scopeTracker)
		scopeTracker = layerGetTracker(baseTracker->parent_layer);
	assert(scopeTracker);

	// call recursive function to compile names
	wleOpGetUniqueNamesRecurse(scopeTracker, scopeTracker->def, baseTracker->def, uids, uniqueNames);

	// cleanup
	eaiDestroy(&uids);
}

/******
* This function performs the opposite of wleOpGetUniqueNamesDescend, instead setting unique names from
* a specified list onto all descendants of the specified tracker.
* PARAMS:
*   rootTracker - GroupTracker root from which the settings of names will propagate
*   scopeDef - GroupDef scope where the names will be set
*   uniqueNames - EArray of unique names to be set on the descendants of rootTracker
*   nameIndex - int pointer to the position in the uniqueNames EArray; incremented after each name is set
******/
static void wleOpSetUniqueNamesDescend(GroupTracker *rootTracker, GroupDef *scopeDef, char **uniqueNames, int *nameIndex)
{
	int i;
	if (rootTracker && scopeDef && *nameIndex < eaSize(&uniqueNames) && groupDefNeedsUniqueName(rootTracker->def))
	{
		TrackerHandle *handle = trackerHandleCreate(rootTracker);
		if (handle)
		{
			groupdbSetUniqueScopeName(scopeDef, handle, NULL, uniqueNames[(*nameIndex)++], false);
			trackerHandleDestroy(handle);
		}
	}
	if (rootTracker)
	{
		for (i = 0; i < rootTracker->child_count; i++)
			wleOpSetUniqueNamesDescend(rootTracker->children[i], scopeDef, uniqueNames, nameIndex);
	}
}


/******
* This function is the recursive workhorse for wleOpSetUniqueNames.
* PARAMS:
*   rootTracker - GroupTracker indicating current target of recursion; examined for a match with the baseDef
*   scopeDef - GroupDef of the scope where names are to be set
*	baseDef - GroupDef to look for in the recursion
*   uidPath - int EArray indicating uid path from trackers matching the baseDef to the tracker to which names will be set
*   uniqueNames - EArray used to store the names of affected trackers
*   nameIndex - int pointer to the index in uniqueNames from which names are currently being used
******/
static void wleOpSetUniqueNamesRecurse(GroupTracker *rootTracker, GroupDef *scopeDef, GroupDef *baseDef, int *uidPath, char **uniqueNames, int *nameIndex)
{
	if (!rootTracker || !scopeDef || !baseDef || !uniqueNames)
		return;
	else if (rootTracker->def == baseDef)
	{
		GroupTracker *trackerCopy = rootTracker;
		int i, j;

		// find corresponding tracker using uid path
		for (i = 0; i < eaiSize(&uidPath); i++)
		{
			int origCount = trackerCopy->child_count;

			for (j = 0; j < trackerCopy->child_count; j++)
			{
				if (trackerCopy->children[j]->uid_in_parent == (U32)uidPath[i])
				{
					trackerCopy = trackerCopy->children[j];
					break;
				}
			}

			// return if function fails to find the corresponding tracker
			if (j >= origCount)
				return;
		}

		wleOpSetUniqueNamesDescend(trackerCopy, scopeDef, uniqueNames, nameIndex);
	}
	else
	{
		int i;

		for (i = 0; i < rootTracker->child_count; i++)
			wleOpSetUniqueNamesRecurse(rootTracker->children[i], scopeDef, baseDef, uidPath, uniqueNames, nameIndex);
	}
}

/******
* This function does the exact opposite of wleOpGetUniqueNames, instead setting names onto
* matching trackers.
* PARAMS:
*   baseTracker - GroupTracker for which to start looking for duplicates
*   tracker - GroupTracker that must be a descendant of baseTracker; function will set names to
*             tracker and its descendants with respect to baseTracker (i.e. for each instance of baseTracker's
*             GroupDef, the function will find each GroupTracker that corresponds to tracker and set the unique
*             names)
*   uniqueNames - EArray that stores the names of affected trackers
*   nameIndex - int pointer updated with the current index being used in uniqueNames
******/
static void wleOpSetUniqueNames(GroupTracker *baseTracker, GroupTracker *tracker, char **uniqueNames, int *nameIndex)
{
	GroupTracker *trackerCopy = tracker;
	GroupTracker *scopeTracker;
	int *uids = NULL;

	if (!baseTracker)
		baseTracker = layerGetTracker(tracker->parent_layer);
	if (!baseTracker)
		return;

	// compile unique uid relative path from tracker to baseTracker
	while (trackerCopy != baseTracker)
	{
		eaiPush(&uids, trackerCopy->uid_in_parent);
		trackerCopy = trackerCopy->parent;
	}
	eaiReverse(&uids);

	// ensure that tracker was a descendant of baseTracker
	assert(trackerCopy);

	// get closest scope to be modified
	scopeTracker = baseTracker->closest_scope->tracker;
	if (!scopeTracker)
		scopeTracker = layerGetTracker(baseTracker->parent_layer);
	assert(scopeTracker);

	// call recursive function to set names
	wleOpSetUniqueNamesRecurse(scopeTracker, scopeTracker->def, baseTracker->def, uids, uniqueNames, nameIndex);

	// cleanup
	eaiDestroy(&uids);
}

static void wleOpCleanUniqueNamesRecurse(GroupTracker *tracker, StashTable cleanedDefs)
{
	if (!tracker || !tracker->def || stashAddressFindPointer(cleanedDefs, tracker->def, NULL))
		return;
	else
	{
		int i;
		for (i = 0; i < tracker->child_count; i++)
			wleOpCleanUniqueNamesRecurse(tracker->children[i], cleanedDefs);
		if (tracker->def->name_to_path && groupIsEditable(tracker->def))
		{
			journalDef(tracker->def);
			groupDefScopeClearInvalidEntries(tracker->def, true);
			assert(stashAddressAddPointer(cleanedDefs, tracker->def, NULL, false));
		}
	}
}

static void wleOpCleanUniqueNames(void)
{
	StashTable cleanedDefs = stashTableCreateAddress(16);
	int i;

	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer)
			wleOpCleanUniqueNamesRecurse(layerGetTracker(layer), cleanedDefs);
	}
	worldUpdateBounds(true, false);
	zmapTrackerUpdate(NULL, false, false);
	stashTableDestroy(cleanedDefs);
}

static void wleOpUnhideTrackers(void)
{
	int i;
	for (i = 0; i < eaSize(&editState.hiddenTrackers); i++)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(editState.hiddenTrackers[i]);
		trackerSetInvisible(tracker, false);
	}
}

/******
* ZONEMAP LOCKING
******/

extern EMEditor worldEditor;
typedef struct WleZoneMapCBData
{
	WleZoneMapCBFn fn;
	void *userdata;
	bool forceGenesisDataLock;
} WleZoneMapCBData;

bool wleZoneMapLockCallback(EMEditor *editor, const char *name, void *state_data, EMResourceState state, void *callback_data, bool success)
{
	WleZoneMapCBData *data = (WleZoneMapCBData*)state_data;
	ZoneMapInfo *zminfo = zmapGetInfo(worldGetPrimaryMap());
	if (!stricmp(zmapInfoGetPublicName(zminfo), name))
	{
		if (state != EMRES_STATE_LOCK_SUCCEEDED)
		{
			emStatusPrintf("Edit FAILED: Couldn't checkout Zone Map %s.", name);
			(data->fn)(zminfo, false, data->userdata);
		}
		else
		{
			emStatusPrintf("Checked out Zone Map %s.", name);
			(data->fn)(zminfo, true, data->userdata);
		}
	}
	SAFE_FREE(data);
	return true;
}

static bool wleOpLockZoneMapConfirm(UIDialog *pDialog, UIDialogButton eButton, WleZoneMapCBData *data)
{
	ZoneMapInfo *zminfo = zmapGetInfo(worldGetPrimaryMap());
	bool unsaved = zmapOrLayersUnsaved(NULL);

	if (eButton == kUIDialogButton_Ok && !unsaved)
	{
		emSetResourceStateWithData(&worldEditor, zmapInfoGetPublicName(zminfo), EMRES_STATE_LOCKING, data);
		zmapInfoLockEx(zminfo, data->forceGenesisDataLock);
	}
	else
	{
		if(eButton == kUIDialogButton_Ok && unsaved)
			Alertf("You must first save your changes before locking the zmap.");
		(data->fn)(zminfo, false, data->userdata);
		SAFE_FREE(data);
	}
	return true;
}

void wleOpLockZoneMapEx(WleZoneMapCBFn fn, void *userdata, bool forceGenesisDataLock)
{
	ZoneMapInfo *zminfo = zmapGetInfo(worldGetPrimaryMap());
	if (!forceGenesisDataLock && zmapInfoLocked(zminfo))
	{
		(fn)(zminfo, true, userdata);
	}
	else
	{
		WleZoneMapCBData *data = calloc(1, sizeof(WleZoneMapCBData));
		data->fn = fn;
		data->userdata = userdata;
		data->forceGenesisDataLock = forceGenesisDataLock;

		ui_WindowShow(UI_WINDOW(ui_DialogCreateEx("Unlocked map", "The Zone Map is not yet locked, would you like to lock it?", wleOpLockZoneMapConfirm, data, NULL, 
			"Cancel", kUIDialogButton_Cancel, "Lock", kUIDialogButton_Ok, NULL)));
	}
}

/******
* CLIENT-SIDE OPERATIONS
******/
void wleOpRefreshUI(void)
{
	wleSetDocSavedBit();
	wleRefreshState();
	wleUITrackerTreeRefresh(NULL);
	wleUILockMngrRefresh();
	wleAERefresh();
	wleUIFileListRefresh();
	wleUIRegionMngrRefresh();
	wleUIMapLayersRefresh();

	if (wl_state.edit_map_game_callback)
	{
		wl_state.edit_map_game_callback(NULL);
	}
}

void wleOpUndoFunction(void *context, JournalEntry *entry)
{
	groupdbUndo(entry);
	wleOpRefreshUI();
	edObjRefreshAllMatrices();
	edObjHarnessGizmoMatrixUpdate();
}

void wleOpFreeFunction(void *context, JournalEntry *entry)
{
	journalEntryDestroy(entry);
}

void wleOpTransactionBegin()
{
	EditUndoStack *stack = edObjGetUndoStack();
	if (!stack)
		return;
	EditUndoBeginGroup(stack);
	groupdbTransactionBegin();
}

void wleOpTransactionEnd()
{
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry = groupdbTransactionEnd();
	if (!stack) return;
	if (entry)
		EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
	EditUndoEndGroup(stack);
}

/******
* GroupDef-related operations
******/

/******
* This is a private function that basically determines whether a list of trackers all have the same
* parent when in EditInstances mode.  This is the validity condition for doing operations in that
* mode.
* PARAMS:
*   handles - TrackerHandle EArray of handles being checked for EditInstances validity
* RETURNS:
*   boolean indicating whether the specified objects are valid for an EditInstances operation
******/
static bool wleEditInstancesValid(TrackerHandle **handles)
{
	// ensure all TrackerHandles have the same parent in EditInstances mode
	if (editState.editOriginal && eaSize(&handles) > 1)
	{
		int i;
		GroupTracker *parent, *tracker;

		assert(tracker = trackerFromTrackerHandle(handles[0]));
		parent = tracker->parent;

		// determine whether all of the selected trackers have the same parent tracker;
		// if not, immediately return false
		for (i = 1; i < eaSize(&handles); i++)
		{
			assert(tracker = trackerFromTrackerHandle(handles[i]))	;
			if (parent != tracker->parent)
				return false;
		}
		return true;
	}
	// if EditInstances mode is off (or if only one TrackerHandle is the target of the operation),
	// then pretty much any non-duplicate set of handles is valid
	else
		return true;
}

static void wleCopyHandles(TrackerHandle ***dest, TrackerHandle **src)
{
	int i;
	for (i = 0; i < eaSize(&src); i++)
		eaPush(dest, trackerHandleCopy(src[i]));
}

float pathNodeAutoconnectMinDist = 0.05;
float pathNodeAutoconnectMaxDist = 25;
AUTO_CMD_FLOAT( pathNodeAutoconnectMinDist, pathNodeAutoconnectMinDist );
AUTO_CMD_FLOAT( pathNodeAutoconnectMaxDist, pathNodeAutoconnectMaxDist );

static void wleOpCreateMain(TrackerHandle *parent, GroupDef **defs, Mat4 *mats, F32 *scales, int index, TrackerHandle ***newHandles)
{
	TrackerHandle *newHandle;
	GroupTracker *tracker;
	int i;

	wleTrackerDeselectAll();

	tracker = trackerFromTrackerHandle(parent);
	assert(tracker && tracker->def);

	// instance parent if necessary
	if (!groupIsPublic(tracker->def) && !editState.editOriginal)
		groupdbInstance(parent, true);

	// perform creation of objects
	for (i = eaSize(&defs) - 1; i >= 0; i--)
	{
		GroupDef *edit_def = objectLibraryGetEditingCopy(defs[i], true, false);
		// make sure model is loaded
		groupPostLoad(edit_def);
		newHandle = groupdbInsertEx(parent, edit_def, mats[i], scales ? scales[i] : 0, NULL, rand(), index);
		tracker = trackerFromTrackerHandle(newHandle);

		// determine whether to instance immediately after placing
		if (tracker && tracker->def)
		{
			bool instanced = false;
			if (tracker->def->property_structs.physical_properties.bInstanceOnPlace)
			{
				instanced = true;
				groupdbInstance(newHandle, true);
				groupdbEditProperty(newHandle, "InstanceOnPlace", NULL);
			}

			tracker = trackerFromTrackerHandle(newHandle);
			if (tracker && tracker->def)
			{
				if (instanced && tracker->def->property_structs.physical_properties.bSubObjectEditOnPlace)
				{
					groupdbEditProperty(newHandle, "SubObjectEditOnPlace", NULL);
					wleTrackerToggleEditSubObject(newHandle);
				}

				if(instanced){
					GroupTracker *parentTracker = trackerFromTrackerHandle(parent);
					// If this is a path node, we need to connect it to other path nodes

					if(tracker->def->property_structs.path_node_properties)
					{
						StashTable stTrackersByDefID = stashTableCreateInt( 256 );

						groupTrackerBuildPathNodeTrackerTable(tracker->parent_layer->grouptree.root_tracker, stTrackersByDefID);
						FOR_EACH_IN_STASHTABLE( stTrackersByDefID, GroupTracker, pCurTracker )
						{
							if (pCurTracker && pCurTracker->def->property_structs.path_node_properties && pCurTracker->def->name_uid != tracker->def->name_uid)
							{
								Vec3 v1, v2;
								F32 fDist = distance3(mats[i][3], pCurTracker->world_path_node->position);
								WorldCollCollideResults wcResults = {0};

								copyVec3(mats[i][3], v1);
								copyVec3(pCurTracker->world_path_node->position, v2);

								//Lets move these up a bit so we don't bump into as much stuff;
								v1[1] += PATH_NODE_Y_OFFSET;
								v2[1] += PATH_NODE_Y_OFFSET;

								wcCapsuleCollideHR(worldGetActiveColl(PARTITION_CLIENT), v1, v2, WC_FILTER_BIT_MOVEMENT, 0, 1.5, &wcResults);

								if(fDist < 25 && fDist > .05)//TODO fix number -dhogberg
								{
									if (!wcResults.hitSomething ||
										(pCurTracker->def->property_structs.path_node_properties->bIsSecret) ||
										(pCurTracker->def->property_structs.path_node_properties->bCanBeObstructed))
									{
										wcCapsuleCollideHR(worldGetActiveColl(PARTITION_CLIENT), v2, v1, WC_FILTER_BIT_MOVEMENT, 0, 1.5, &wcResults);

										if(fDist < pathNodeAutoconnectMaxDist && fDist > pathNodeAutoconnectMinDist)
										{
											if (!wcResults.hitSomething ||
												(pCurTracker->def->property_structs.path_node_properties->bIsSecret ||
												pCurTracker->def->property_structs.path_node_properties->bCanBeObstructed))
											{
												WorldPathEdge *pEdge = StructCreate(parse_WorldPathEdge);
												pEdge->uOther = pCurTracker->def->name_uid;

												eaPush(&tracker->def->property_structs.path_node_properties->eaConnections, pEdge);

												pEdge = StructCreate(parse_WorldPathEdge);
												pEdge->uOther = tracker->def->name_uid;

												eaPush(&pCurTracker->def->property_structs.path_node_properties->eaConnections, pEdge);
											}
										}
									}
								}
							}
						}
						FOR_EACH_END;

						stashTableDestroy( stTrackersByDefID );
					}
				}//End new stuff

				if (tracker->def->property_structs.physical_properties.bHideOnPlace)
				{
					wleTrackerHide(newHandle);
				}
			}
		}

		if (tracker && newHandle && newHandles)
			eaPush(newHandles, newHandle);
		else
			trackerHandleDestroy(newHandle);
	}
}

/******
* This function creates new objects in the world.
* PARAMS:
*   parentHandle - the TrackerHandle to the new objects' parent
*   defs - GroupDef EArray of the objects to add to the parent
*   mats - Mat4 array where each respective def will be placed
*   index - int index where the defs will be inserted into the parent
*   newHandles - TrackerHandle EArray of the handles to the trackers that were created in this operation
******/
void wleOpCreateEx(const TrackerHandle *parentHandle, GroupDef **defs, Mat4 *mats, F32 *scales, int index, TrackerHandle ***newHandles)
{
	GroupTracker *tracker;
	TrackerHandle *parentCopy;
	TrackerHandle **createdHandles = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	const char *filename;
	bool origLocked = editState.lockedSelection;
	bool validated = true;
	int i;

	// validate: if nothing is being created, just return without an error
	if (eaSize(&defs) == 0)
		return;

	// validate: new parent is modifiable
	if (!wleTrackerIsEditable(parentHandle, false, true, true))
		validated = false;

	for (i = 0; i < eaSize(&defs); i++)
	{
		// validate: no layer defs
		if (defs[i]->name_uid == 0)
		{
			emStatusPrintf("Layer \"%s\" cannot be reparented!", defs[i]->name_str);
			validated = false;
		}
	}

	// validate index
	assert((tracker = trackerFromTrackerHandle(parentHandle)) && tracker->def);
	if (index >= tracker->child_count)
		index = -1;

	// validate: new parent is not a model (i.e. its not a rootmods file)
	filename = groupDefGetFilename(tracker->def);
	if (filename && strEndsWith(filename, ".modelnames"))
	{
		emStatusPrintf("Cannot add children to model library pieces.");
		validated = false;
	}

	if (!validated)
		return;

	editState.lockedSelection = false;
	parentCopy = trackerHandleCopy(parentHandle);
	assert(parentCopy);

	// start undo journal entry
	if (stack)
		EditUndoBeginGroup(stack);
	groupdbTransactionBegin();

	// perform main creation code
	wleOpCreateMain(parentCopy, defs, mats, scales, index, &createdHandles);
	entry = groupdbTransactionEnd();
	if (entry && stack)
		EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);

	// select new handles
	wleTrackerSelectList(createdHandles, true);

	// end undo group
	if (stack)
		EditUndoEndGroup(stack);

	editState.lockedSelection = origLocked;

	// update UI
	emStatusPrintf("Placed objects.");
	wleOpRefreshUI();

	if (newHandles)
	{
		eaPushEArray(newHandles, &createdHandles);
		eaDestroy(&createdHandles);
	}
	else
		eaDestroyEx(&createdHandles, trackerHandleDestroy);
	
	trackerHandleDestroy(parentCopy);
}

/******
* This function creates new objects in the world.
* PARAMS:
*   parentHandles - the TrackerHandle to the new objects' parents
*   defs - GroupDef EArray of the objects to add to the parents
*   mats - Mat4 arrays where each respective def will be placed
*   indices - int indices where the defs will be inserted into the parents
*   newHandles - TrackerHandle EArray of the handles to the trackers that were created in this operation
******/
void wleOpCreateList(const TrackerHandle **parentHandles, GroupDef **defs, Mat4 **mats, F32 *scales, int *indices, TrackerHandle ***newHandles, bool performSelection)
{
	GroupTracker *tracker;
	TrackerHandle *parentCopy;
	TrackerHandle **createdHandles = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	const char *filename;
	bool origLocked = editState.lockedSelection;
	bool validated = true;
	int i, j;

	// validate: if nothing is being created, just return without an error
	if (eaSize(&defs) == 0)
	{
		return;
	}

	for (i = 0; i < eaiSize(&indices); i++)
	{
		// validate: new parent is modifiable
		if (!wleTrackerIsEditable(parentHandles[i], false, true, true))
		{
			continue;
		}

		for (j = 0; j < eaSize(&defs); j++)
		{
			// validate: no layer defs
			if (defs[j]->name_uid == 0)
			{
				emStatusPrintf("Layer \"%s\" cannot be reparented!", defs[j]->name_str);
				break;
			}
		}
		if (j < eaSize(&defs))
		{
			continue;
		}

		// validate index
		assert((tracker = trackerFromTrackerHandle(parentHandles[i])) && tracker->def);
		if (indices[i] >= tracker->child_count)
		{
			indices[i] = -1;
		}

		// validate: new parent is not a model (i.e. its not a rootmods file)
		filename = groupDefGetFilename(tracker->def);
		if (filename && strEndsWith(filename, ".modelnames"))
		{
			emStatusPrintf("Cannot add children to model library pieces.");
			continue;
		}

		editState.lockedSelection = false;
		parentCopy = trackerHandleCopy(parentHandles[i]);
		assert(parentCopy);

		if (!i)
		{
			// start undo journal entry
			if (stack)
			{
				EditUndoBeginGroup(stack);
			}
			groupdbTransactionBegin();
		}

		// perform main creation code
		wleOpCreateMain(parentCopy, defs, mats[i], scales, indices[i], &createdHandles);

		trackerHandleDestroy(parentCopy);
	}

	entry = groupdbTransactionEnd();
	if (entry && stack)
	{
		EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
	}

	if (performSelection)
	{
		// select new handles
		wleTrackerSelectList(createdHandles, true);
	}

	// end undo group
	if (stack)
	{
		EditUndoEndGroup(stack);
	}

	editState.lockedSelection = origLocked;

	// update UI
	emStatusPrintf("Placed objects.");
	wleOpRefreshUI();

	if (newHandles)
	{
		eaPushEArray(newHandles, &createdHandles);
		eaDestroy(&createdHandles);
	}
	else
	{
		eaDestroyEx(&createdHandles, trackerHandleDestroy);
	}
}

/******
* This function creates a new object in the world.
* PARAMS:
*   parentHandle - the TrackerHandle to the new object's parent
*   uid - the definition ID of the new object to create
*   mat - the position/rotation Mat4 of the new object
******/
TrackerHandle *wleOpCreate(const TrackerHandle *parentHandle, int uid, Mat4 mat, F32 scale)
{
	GroupDef *objLibDef = objectLibraryGetGroupDef(uid, true);

	if (objLibDef)
	{
		GroupDef **defs = NULL;
		TrackerHandle **newHandles = NULL;
		TrackerHandle *ret;

		eaPush(&defs, objLibDef);
		wleOpCreateEx(parentHandle, defs, (Mat4*)&mat[0][0], &scale, -1, &newHandles);
		ret = trackerHandleCopy(eaSize(&newHandles) > 0 ? newHandles[0] : NULL);
		eaDestroyEx(&newHandles, trackerHandleDestroy);
		return ret;
	}
	else
	{
		emStatusPrintf("Could not find object library piece matching UID %i.", uid);
		return NULL;
	}
}

/******
* This function increments the mod time of an existing tracker.
* PARAMS:
*   handles - the TrackerHandles to the trackers to touch
******/
void wleOpTouch(const TrackerHandle **handles)
{
	EditUndoStack *stack = edObjGetUndoStack();
	bool origLocked = editState.lockedSelection, anyIsObjLib, found;
	GroupTracker *tracker;
	JournalEntry *entry;
	ZoneMapLayer **layers = NULL;
	int i, j;

	anyIsObjLib = false;

	if (eaSize(&handles) == 0)
	{
		return;
	}

	for (i = 0; i < eaSize(&handles); i++)
	{
		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->def);

		if (!i)
		{
			editState.lockedSelection = false;
			if (stack)
			{
				EditUndoBeginGroup(stack);
			}
			groupdbTransactionBegin();
		}

		groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);

		if (tracker->def)
		{
			groupdbUpdateBounds(tracker->def->def_lib);
		}

		emStatusPrintf("Touched \"%s\".", tracker->def->name_str);

		if (groupIsObjLib(tracker->def))
		{
			anyIsObjLib = true;
		}
		else if (!anyIsObjLib)
		{
			found = false;
			for (j = 0; j < eaSize(&layers); j++)
			{
				if (layers[j] == tracker->parent_layer)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				eaPush(&layers, tracker->parent_layer);
			}
		}
	}

	if (anyIsObjLib)
	{
		zmapTrackerUpdate(NULL, false, false);
	}
	else
	{
		for (i = 0; i < eaSize(&layers); i++)
		{
			layerTrackerUpdate(layers[i], false, false);
		}
	}
	eaDestroy(&layers);
	
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
		{
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		}
		EditUndoEndGroup(stack);
	}
	editState.lockedSelection = origLocked;

	wleOpRefreshUI();
	wleUISearchClearUniqueness();
}

/******
* This function creates a new instance of an existing tracker.
* PARAMS:
*   handle - the TrackerHandle to the tracker to instance
******/
void wleOpInstance(const TrackerHandle *handle)
{
	GroupTracker *tracker;
	char *oldName;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	TrackerHandle *parentHandle;
	bool origLocked = editState.lockedSelection;
	char **uniqueNames = NULL;
	int nameIndex = 0;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	// validate: not instancing a layer
	if (!tracker->parent)
	{
		emStatusPrintf("Layers cannot be instanced!");
		return;
	}
	parentHandle = trackerHandleCreate(tracker->parent);
	assert(parentHandle);

	// validate: user is allowed to modify parent
	if (!wleTrackerIsEditable(parentHandle, false, true, true))
		return;

	editState.lockedSelection = false;
	strdup_alloca(oldName, tracker->def->name_str);

	if (stack)
		EditUndoBeginGroup(stack);
	groupdbTransactionBegin();
	if (!groupIsPublic(tracker->parent->def) && !editState.editOriginal)
		groupdbInstance(parentHandle, true);
	wleOpGetUniqueNames(trackerFromTrackerHandle(parentHandle), trackerFromTrackerHandle(handle), &uniqueNames);
	groupdbInstance(handle, false);
	wleOpSetUniqueNames(trackerFromTrackerHandle(parentHandle), trackerFromTrackerHandle(handle), uniqueNames, &nameIndex);
	groupdbEditProperty(handle, "InstanceOnPlace", NULL);
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	tracker = trackerFromTrackerHandle(handle);
	if (tracker && tracker->def)
		emStatusPrintf("Instanced \"%s\" into \"%s\".", oldName, tracker->def->name_str);
	editState.lockedSelection = origLocked;

	eaDestroyEx(&uniqueNames, NULL);
	trackerHandleDestroy(parentHandle);
	wleOpRefreshUI();
	wleUISearchClearUniqueness();
}

/******
* This function renames a tracker.
* PARAMS:
*   handle - the TrackerHandle to the group
*   newName - the new name of the group
* RETURNS:
*   bool indicating whether the operation succeeded
******/
bool wleOpRename(const TrackerHandle *handle, const char *newName)
{
	GroupTracker *tracker;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	// validate: newName is valid
	if (!newName || !newName[0])
	{
		emStatusPrintf("No name specified for rename.");
		return false;
	}
	else if (strchr(newName, '/') || strchr(newName, '\\'))
	{
		emStatusPrintf("Tracker names cannot contain the characters '/' or '\\'.");
		return false;
	}

	if (!groupLibIsValidGroupName(tracker->def->def_lib, newName, tracker->def->root_id))
	{
		if (tracker->def->root_id != 0)
			emStatusPrintf("Group name must be unique in Object Library piece.");
		else if (tracker->def->def_lib->zmap_layer)
			emStatusPrintf("Group name must be unique in layer.");
		else
			emStatusPrintf("Group name must be unique in Object Library.");
		return false;
	}

	// validate: newName is different from old name
	if (strcmp(newName, tracker->def->name_str) == 0)
		return false;

	// validate: user is allowed to modify tracker
	if (!wleTrackerIsEditable(handle, false, true, false))
		return false;

	emStatusPrintf("Renamed \"%s\" to \"%s\".", tracker->def->name_str, newName);

	if (stack)
		EditUndoBeginGroup(stack);

	// instance if necessary
	groupdbTransactionBegin();
	if (!groupIsPublic(tracker->def) && !editState.editOriginal)
		groupdbInstance(handle, true);

	groupdbRename(handle, newName);
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// handle UI changes
	wleOpRefreshUI();
	wleUISearchClearUniqueness();

	return true;
}

/******
* This function sets tags on a group.
* PARAMS:
*   handle - the TrackerHandle to the group
*   newName - the new name of the group
* RETURNS:
*   bool indicating whether the operation succeeded
******/
bool wleOpSetTags(const TrackerHandle *handle, const char *newTags)
{
	GroupTracker *tracker;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	// validate: newTags is different from old tags
	if (stricmp_safe(newTags, tracker->def->tags) == 0)
		return false;

	// validate: user is allowed to modify tracker
	if (!wleTrackerIsEditable(handle, false, true, false))
		return false;

	if (stack)
		EditUndoBeginGroup(stack);

	// instance if necessary
	groupdbTransactionBegin();
	if (!groupIsPublic(tracker->def) && !editState.editOriginal)
		groupdbInstance(handle, true);

	groupdbSetTags(handle, newTags);
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// handle UI changes
	wleOpRefreshUI();

	return true;
}

/******
* This function creates a new group out of existing objects, creating the group underneath the objects'
* "lowest" common parent.
* PARAMS:
*   handles - TrackerHandle array to the objects being moved into the new group
*   pivotMat - Mat4 pivot in world coordinates for the new group
******/
void wleOpGroup(TrackerHandle **handles, Mat4 pivotMat)
{
	bool validated = true;
	TrackerHandle *parentHandle, *newGroupHandle;
	TrackerHandle **copiedHandles = NULL;
	GroupTracker *tracker;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	int i;
	bool origLocked = editState.lockedSelection;
	char **uniqueNames = NULL;

	// validate: something is actually being grouped
	if (eaSize(&handles) == 0)
	{
		emStatusPrintf("Something must be selected first before grouping!");
		return;
	}

	// validate: valid EditInstances selection
	if (!wleEditInstancesValid(handles))
	{
		emStatusPrintf("When EditInstances is enabled, the selection cannot span across different groups!");
		return;
	}

	// validate: selection all belong to the same layer
	parentHandle = wleFindCommonParent(handles);
	if (!parentHandle)
	{
		emStatusPrintf("Cannot group across layers!");
		return;
	}

	// validate: new group's destination is modifiable
	if (!wleTrackerIsEditable(parentHandle, false, true, true))
		validated = false;

	for (i = 0; i < eaSize(&handles); i++)
	{
		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->def);

		// validate: no target trackers are layers
		if (!tracker->parent)
		{
			emStatusPrintf("Layer \"%s\" cannot be grouped!", tracker->def->name_str);
			validated = false;
		}
		
		// validate: target tracker's parent is modifiable
		if (!wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, true, true))
			validated = false;
	}

	if (!validated)
	{
		trackerHandleDestroy(parentHandle);
		return;
	}

	editState.lockedSelection = false;
	wleCopyHandles(&copiedHandles, handles);
	if (stack)
		EditUndoBeginGroup(stack);
	wleTrackerDeselectAll();

	// instance parent if necessary
	assert(tracker = trackerFromTrackerHandle(parentHandle));
	groupdbTransactionBegin();
	if (!groupIsPublic(tracker->def) && !editState.editOriginal)
		groupdbInstance(parentHandle, true);

	// create the new group
	newGroupHandle = groupdbCreate(parentHandle, 0, pivotMat, 0, -1);

	if (newGroupHandle)
	{
		GroupTracker *newGroup;
		int idx = wleFindCommonParentIndex(parentHandle, copiedHandles);

		if (idx >= 0)
			groupdbMoveToIndex(newGroupHandle, idx);

		newGroup = trackerFromTrackerHandle(newGroupHandle);
		assert(newGroup && newGroup->def);
		emStatusPrintf("Grouped selection into \"%s\".", newGroup->def->name_str);

		// create copies of the specified trackers in the new group and delete the old trackers
		for(i = 0; i < eaSize(&copiedHandles); i++)
		{
			TrackerHandle *tempNewHandle;

			if (tempNewHandle = groupdbInsert(newGroupHandle, copiedHandles[i], -1, false))
			{
				TrackerHandle *tempParentHandle;
				int nameIndex = 0;

				assert(tracker = trackerFromTrackerHandle(copiedHandles[i]));
				assert(tempParentHandle = trackerHandleCreate(tracker->parent));

				// instance parent
				if (!groupIsPublic(tracker->parent->def) && !editState.editOriginal)
					groupdbInstance(tempParentHandle, true);

				// cache unique names
				tracker = trackerFromTrackerHandle(copiedHandles[i]);
				wleOpGetUniqueNames(trackerFromTrackerHandle(parentHandle), tracker, &uniqueNames);

				// delete original tracker
				groupdbDelete(copiedHandles[i]);

				// restore unique names
				newGroup = trackerFromTrackerHandle(newGroupHandle);
				tracker = trackerFromTrackerHandle(tempNewHandle);
				wleOpSetUniqueNames(newGroup, tracker, uniqueNames, &nameIndex);
				eaClearEx(&uniqueNames, NULL);

				trackerHandleDestroy(tempParentHandle);
			}
			trackerHandleDestroy(tempNewHandle);
		}
		wleOpCleanUniqueNames();
	}

	entry = groupdbTransactionEnd();
	if (entry && stack)
		EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);

	// adjust UI
	wleOpRefreshUI();
	wleUISearchClearUniqueness();
	wleTrackerSelect(newGroupHandle, true);
	if (stack)
		EditUndoEndGroup(stack);
	editState.lockedSelection = origLocked;

	// cleanup
	eaDestroyEx(&uniqueNames, NULL);
	eaDestroyEx(&copiedHandles, trackerHandleDestroy);
	trackerHandleDestroy(newGroupHandle);
	trackerHandleDestroy(parentHandle);
}

/******
* This function moves trackers into an existing group.
* PARAMS:
*   destHandle - TrackerHandle to the group where the trackers will be moved
*   handles - EArray of TrackerHandles to the trackers that are being moved
*   index - int index at which to insert the handles into the parent
******/
void wleOpAddToGroup(const TrackerHandle *destHandle, TrackerHandle **handles, int index)
{
	bool validated = true;
	GroupTracker *tracker, *parent_tracker;
	TrackerHandle **newHandles = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	const char *filename;
	int i;
	bool origLocked = editState.lockedSelection;

	// validate: if nothing is being added to a group, just return without an error
	if (eaSize(&handles) == 0)
		return;

	// validate: valid EditInstances selection
	if (!wleEditInstancesValid(handles))
	{
		emStatusPrintf("When EditInstances is enabled, the selection cannot span across different groups!");
		return;
	}

	// validate: new parent is modifiable
	if (!wleTrackerIsEditable(destHandle, false, true, true))
		validated = false;

	parent_tracker = trackerFromTrackerHandle(destHandle);

	for (i = 0; i < eaSize(&handles); i++)
	{
		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->def);

		// validate: no trackers being re-parented are layers
		if (!tracker->parent)
		{
			emStatusPrintf("Layer \"%s\" cannot be reparented!", tracker->def->name_str);
			validated = false;
		}

		// validate: re-parented trackers' old parents are modifiable
		if (!wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, true, true))
			validated = false;

		if (!groupdbLoopCheck(parent_tracker->def, tracker->def))
		{
			emStatusPrintf("Cannot insert group \"%s\" into its own child!", tracker->def->name_str);
			validated = false;
		}
	}

	// validate index
	assert((tracker = trackerFromTrackerHandle(destHandle)) && tracker->def);
	if (index >= tracker->child_count)
		index = -1;

	// validate: new parent is not a model (i.e. its not a rootmods file)
	filename = groupDefGetFilename(tracker->def);
	if (validated && filename && strEndsWith(filename, ROOTMODS_EXTENSION))
	{
		emStatusPrintf("Cannot add children to model library pieces.");
		validated = false;
	}

	// validate: new parent is allowed to have children
	if (validated && (tracker->def->property_structs.patrol_properties || tracker->def->property_structs.encounter_properties))
	{
		emStatusPrintf("Cannot add group children to patrols or encounters.");
		validated = false;
	}

	if (!validated)
		return;

	editState.lockedSelection = false;
	if (stack)
		EditUndoBeginGroup(stack);
	wleTrackerDeselectAll();

	groupdbTransactionBegin();
	if (!groupIsPublic(tracker->def) && !editState.editOriginal)
		groupdbInstance(destHandle, true);

	// perform insertion
	wleOpUnhideTrackers();
	for(i = 0; i < eaSize(&handles); i++)
	{
		TrackerHandle *tempNewHandle;

		// group insertion
		if (tempNewHandle = groupdbInsert(destHandle, handles[i], index < 0 ? -1 : index + i, false))
			eaPush(&newHandles, tempNewHandle);
	}

	// perform deletion of inserted trackers
	assert(eaSize(&newHandles) == eaSize(&handles));
	for (i = 0; i < eaSize(&newHandles); i++)
	{
		TrackerHandle *tempNewHandle = newHandles[i];

		const TrackerHandle **paramHandles = NULL;
		TrackerHandle *commonParent;
		TrackerHandle *tempParentCopy;
		char **uniqueNames = NULL;
		int nameIndex = 0;

		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->parent);
		assert(tempParentCopy = trackerHandleCreate(tracker->parent));

		// instance deleted tracker's if necessary
		if (!groupIsPublic(tracker->parent->def) && !editState.editOriginal)
			groupdbInstance(tempParentCopy, true);

		// find closest common parent between source and destination
		eaPush(&paramHandles, destHandle);
		eaPush(&paramHandles, handles[i]);
		commonParent = wleFindCommonParent(paramHandles);

		// cache unique names
		wleOpGetUniqueNames(trackerFromTrackerHandle(commonParent), trackerFromTrackerHandle(handles[i]), &uniqueNames);

		// delete the original tracker
		groupdbDelete(handles[i]);

		// reset unique names
		wleOpSetUniqueNames(trackerFromTrackerHandle(commonParent), trackerFromTrackerHandle(tempNewHandle), uniqueNames, &nameIndex);
		eaDestroyEx(&uniqueNames, NULL);

		trackerHandleDestroy(tempParentCopy);
		trackerHandleDestroy(commonParent);
		eaDestroy(&paramHandles);
	}
	wleOpCleanUniqueNames();

	tracker = trackerFromTrackerHandle(destHandle);
	if (tracker && tracker->def)
		emStatusPrintf("Added selection to \"%s\".", tracker->def->name_str);
	entry = groupdbTransactionEnd();
	if (entry && stack)
		EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);

	// handle UI changes
	wleOpRefreshUI();
	wleUISearchClearUniqueness();
	wleTrackerSelectList(newHandles, true);
	if (stack)
		EditUndoEndGroup(stack);
	editState.lockedSelection = origLocked;
	eaDestroyEx(&newHandles, trackerHandleDestroy);
}


/******
* This function ungroups an existing group, deleting the selected group and putting its children
* into the group's parent.
* PARAMS:
*   handle - TrackerHandle to the group to ungroup
******/
void wleOpUngroup(const TrackerHandle *handle)
{
	GroupTracker *tracker, *scopeTracker;
	TrackerHandle *parentHandle, *scopeHandle;
	TrackerHandle **childHandles = NULL;
	TrackerHandle **newChildHandles = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	char **uniqueNames = NULL;
	int nameIndex = 0;
	int i, trackerIdx;
	bool origLocked = editState.lockedSelection;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	// validate: target is not a layer
	if (!tracker->parent)
	{
		emStatusPrintf("Layer \"%s\" cannot be ungrouped!", tracker->def->name_str);
		return;
	}
	assert(parentHandle = trackerHandleCreate(tracker->parent));

	// validate: target has children
	if (eaSize(&tracker->def->children) == 0)
	{
		emStatusPrintf("Cannot ungroup \"%s\" because it has no children!", tracker->def->name_str);
		return;
	}

	// validate: target is not a public object library piece
	if (groupIsPublic(tracker->def))
	{
		emStatusPrintf("Cannot ungroup public object library piece \"%s\"!", tracker->def->name_str);
		return;
	}

	// validate: target is modifiable
	if (!wleTrackerIsEditable(handle, false, true, true))
		return;

	editState.lockedSelection = false;
	if (stack)
		EditUndoBeginGroup(stack);
	wleTrackerDeselectAll();
	emStatusPrintf("Ungrouped \"%s\".", tracker->def->name_str);

	// instance if necessary
	groupdbTransactionBegin();
	groupdbInstance(handle, !editState.editOriginal);

	// collect TrackerHandles to the children
	assert(tracker = trackerFromTrackerHandle(handle));
	scopeTracker = trackerGetScopeTracker(tracker);
	scopeHandle = trackerHandleCreate(scopeTracker);
	trackerIdx = tracker->idx_in_parent;
	for (i = 0; i < tracker->child_count; i++)
		eaPush(&childHandles, trackerHandleCreate(tracker->children[i]));

	// insert all of the children into the parent GroupDef
	for (i = 0; i < eaSize(&childHandles); i++)
	{
		TrackerHandle *newChild = groupdbInsert(parentHandle, childHandles[i], trackerIdx++, false);
		if (newChild)
		{
			GroupTracker *childTracker;

			// cache child names
			childTracker = trackerFromTrackerHandle(childHandles[i]);
			wleOpGetUniqueNames(trackerFromTrackerHandle(parentHandle), childTracker, &uniqueNames);

			eaPush(&newChildHandles, newChild);
		}
	}

	// delete the ungrouped group from its parent
	groupdbDelete(handle);

	// reset cached unique names and clean unused names/paths
	scopeTracker = trackerFromTrackerHandle(scopeHandle);
	for (i = 0; i < eaSize(&newChildHandles); i++)
	{
		GroupTracker *childTracker = trackerFromTrackerHandle(newChildHandles[i]);
		wleOpSetUniqueNames(trackerFromTrackerHandle(parentHandle), childTracker, uniqueNames, &nameIndex);
	}
	wleOpCleanUniqueNames();

	entry = groupdbTransactionEnd();
	if (entry && stack)
		EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);

	// handle UI changes
	wleOpRefreshUI();
	wleUISearchClearUniqueness();
	wleTrackerSelectList(newChildHandles, true);
	if (stack)
		EditUndoEndGroup(stack);
	editState.lockedSelection = origLocked;
	eaDestroyEx(&uniqueNames, NULL);
	eaDestroyEx(&newChildHandles, trackerHandleDestroy);
	eaDestroyEx(&childHandles, trackerHandleDestroy);
	trackerHandleDestroy(parentHandle);
	trackerHandleDestroy(scopeHandle);
}

/******
* This function moves objects around in the world.
* PARAMS:
*   handles - TrackerHandle array to the trackers being moved
*   mats - Mat4 array with the destination world matrices corresponding to each of the handles
* RETURNS:
*   bool indicating whether the move occurred or failed due to validation error
******/
bool wleOpMove(const TrackerHandle **handles, Mat4 *mats)
{
	bool validated = true;
	GroupTracker *tracker;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	int i;

	// validate: something is being moved; otherwise, quietly return
	if (eaSize(&handles) == 0)
		return false;

	for (i = 0; i < eaSize(&handles); i++)
	{
		assert(tracker = trackerFromTrackerHandle(handles[i]));

		// validate: none of the trackers are layers
		if (!tracker->parent)
		{
			emStatusPrintf("Layer \"%s\" cannot be moved!", tracker->def ? tracker->def->name_str : "DELETED");
			validated = false;
		}

		// validate: each of the trackers' parents are modifiable
		if (!wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, true, true))
			validated = false;
	}

	if (!validated)
		return false;

	if (stack)
		EditUndoBeginGroup(stack);

	groupdbTransactionBegin();
	for(i = 0; i < eaSize(&handles); i++)
	{
		TrackerHandle *parentHandle;

		// instance parent if necessary
		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->parent);
		assert(parentHandle = trackerHandleCreate(tracker->parent));
		if (!groupIsPublic(tracker->parent->def) && !editState.editOriginal)
			groupdbInstance(parentHandle, true);

		// move trackers
		groupdbMove(handles[i], mats[i]);

		tracker = trackerFromTrackerHandle(handles[i]);

		if (tracker->def && tracker->parent)
		{
			terrainSourceUpdateObjectsByDef(tracker->parent_layer->terrain.source_data, tracker->def);
		}

		trackerHandleDestroy(parentHandle);

		tracker = trackerFromTrackerHandle(handles[i]);
		if (tracker->def && tracker->def->property_structs.curve && groupIsEditable(tracker->def))
		{
			curveApplyConstraints(tracker);
			groupdbDirtyDef(tracker->def, UPDATE_GROUP_PROPERTIES);
			groupdbUpdateBounds(tracker->def->def_lib);
			zmapTrackerUpdate(NULL, false, false);
		}
	}
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// handle UI changes
	wleOpRefreshUI();
	return true;
}


/******
* This function sets scaling factors
* PARAMS:
*   handles - TrackerHandle array to the trackers being moved
*   scales - Vec3 array with the scale vectors corresponding to each of the handles
* RETURNS:
*   bool indicating whether the scale occurred or failed due to validation error
******/
bool wleOpSetScale(TrackerHandle **handles, Vec3 *scales)
{
	bool validated = true;
	GroupTracker *tracker;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	int i;

	// validate: something is being scaled; otherwise, quietly return
	if (eaSize(&handles) == 0)
		return false;

	for (i = 0; i < eaSize(&handles); i++)
	{
		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->def);

		// validate: each of the trackers is modifiable
		if (!wleTrackerIsEditable(handles[i], false, true, false))
			validated = false;
	}

	if (!validated)
		return false;

	if (stack)
		EditUndoBeginGroup(stack);
	emStatusPrintf("Scaled selection.");

	groupdbTransactionBegin();
	for(i = 0; i < eaSize(&handles); i++)
	{
		// instance if necessary
		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->def);
		if (!groupIsPublic(tracker->def) && !editState.editOriginal)
			groupdbInstance(handles[i], true);
		groupdbSetScale(handles[i], scales[i]);
	}
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// handle UI changes
	wleOpRefreshUI();
	return true;
}


/******
* This function creates copies of a number of objects in the world.
* PARAMS:
*   parent - TrackerHandle to where the trackers will be copied.
*   handles - TrackerHandle EArray of the trackers that will be copied.
******/
bool wleOpCopyEx(const TrackerHandle *parent, TrackerHandle **handles, bool finishUp)
{
	GroupTracker *tracker;
	TrackerHandle *parentCopy;
	TrackerHandle **copiedOrigHandles = NULL;
	TrackerHandle **copiedHandles = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	int i, idx;
	bool origLocked = editState.lockedSelection;

	assert((tracker = trackerFromTrackerHandle(parent)) && tracker->def);

	// validate: something is being copied; otherwise, quietly return
	if (eaSize(&handles) == 0)
		return false;

	// validate: parent is editable
	if (!wleTrackerIsEditable(parent, false, true, true))
		return false;

	editState.lockedSelection = false;
	parentCopy = trackerHandleCopy(parent);
	assert(parentCopy);
	wleCopyHandles(&copiedOrigHandles, handles);
	if (stack)
		EditUndoBeginGroup(stack);
	edObjSelectionClear(EDTYPE_TRACKER);
	emStatusPrintf("Copied selection into \"%s\".", tracker->def->name_str);

	// instance parent if necessary
	groupdbTransactionBegin();
	if (!groupIsPublic(tracker->def) && !editState.editOriginal)
		groupdbInstance(parent, true);

	idx = wleFindCommonParentIndex(parentCopy, copiedOrigHandles);
	for (i = 0; i < eaSize(&copiedOrigHandles); i++)
	{
		TrackerHandle *copy;
		copy = groupdbInsert(parentCopy, copiedOrigHandles[i], idx, true);
		if (copy)
		{
			eaPush(&copiedHandles, copy);
		}
	}
	entry = groupdbTransactionEnd();
	if (stack && entry)
		EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);

	// handle UI changes
	if (finishUp)
	{
		wleOpRefreshUI();
		wleUISearchClearUniqueness();
	}
	wleTrackerSelectList(copiedHandles, true);
	if (stack)
		EditUndoEndGroup(stack);
	editState.lockedSelection = origLocked;
	eaDestroyEx(&copiedHandles, trackerHandleDestroy);
	eaDestroyEx(&copiedOrigHandles, trackerHandleDestroy);
	trackerHandleDestroy(parentCopy);

	return true;
}

/******
* This function deletes trackers from the world.
* PARAMS:
*   handles - TrackerHandle EArray containing handles to all trackers being deleted
*   verbose - bool indicating whether to print a status message when complete
* RETURNS:
*   bool indicating whether the deletion occurred without errors
******/
bool wleOpDeleteEx(TrackerHandle **handles, bool verbose)
{
	bool validated = true;
	int i;
	GroupTracker *tracker;
	TrackerHandle *parentHandle;
	TrackerHandle **copiedHandles = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	bool origLocked = editState.lockedSelection;

	// validate: something is being deleted; otherwise, quietly return
	if (eaSize(&handles) == 0)
		return false;

	// validate: trackers have same parent if EditInstances is enabled
	if (!wleEditInstancesValid(handles))
	{
		emStatusPrintf("When EditInstances is enabled, the deletion cannot span across different groups!");
		return false;
	}

	for (i = 0; i < eaSize(&handles); i++)
	{
		assert(tracker = trackerFromTrackerHandle(handles[i]));

		// validate: none of the trackers are layers
		if (!tracker->parent)
		{
			emStatusPrintf("Select and delete layer \"%s\" separately!", tracker->def ? tracker->def->name_str : "DELETED");
			return false;
		}

		// validate: each of the trackers' parents are modifiable
		if (!wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, true, true))
			validated = false;
	}

	if (!validated)
		return false;

	editState.lockedSelection = false;
	wleCopyHandles(&copiedHandles, handles);
	EditUndoBeginGroup(stack);
	edObjSelectionClear(EDTYPE_TRACKER);
	if (verbose)
		emStatusPrintf("Deleted selection.");

	groupdbTransactionBegin();
	for(i = 0; i < eaSize(&copiedHandles); i++)
	{
		TrackerHandle *closestScopeHandle;

		assert((tracker = trackerFromTrackerHandle(copiedHandles[i])) && tracker->parent);
		assert(parentHandle = trackerHandleCreate(tracker->parent));
		closestScopeHandle = trackerHandleCreate(tracker->parent->closest_scope->tracker ? tracker->parent->closest_scope->tracker : layerGetTracker(tracker->parent_layer));

		// instance as necessary and perform the deletion
		if (!groupIsPublic(tracker->parent->def) && !editState.editOriginal)
			groupdbInstance(parentHandle, true);

		//Remove path node connections
		if (tracker->world_path_node)
		{
			StashTable stTrackersByDefID = stashTableCreateInt( 256 );
			groupTrackerBuildPathNodeTrackerTable(tracker->parent_layer->grouptree.root_tracker, stTrackersByDefID);
			FOR_EACH_IN_STASHTABLE( stTrackersByDefID, GroupTracker, pSibling )
			{
				if(pSibling->world_path_node && pSibling->world_path_node->uID != tracker->world_path_node->uID)
				{
					int k;
					//TODO see if we need to do some better NULL checking here -dhogberg
					for (k = 0; k < eaSize(&pSibling->def->property_structs.path_node_properties->eaConnections); ++k)
					{
						WorldPathEdge *pEdge = pSibling->def->property_structs.path_node_properties->eaConnections[k];

						if (pEdge && pEdge->uOther == tracker->world_path_node->uID)
						{
							eaRemove(&pSibling->def->property_structs.path_node_properties->eaConnections, k);
							StructDestroy(parse_WorldPathEdge, pEdge);
							break;// Hopefully we don't have more than one edge with that id
						}
					}
				}
			}
			FOR_EACH_END;
			stashTableDestroy( stTrackersByDefID );
		}

		trackerHandleDestroy(parentHandle);
		trackerHandleDestroy(closestScopeHandle);
	}
	groupdbDeleteList(copiedHandles);
	wleOpCleanUniqueNames();
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// handle UI changes
	editState.lockedSelection = origLocked;
	wleOpRefreshUI();
	eaDestroyEx(&copiedHandles, trackerHandleDestroy);

	return true;
}

void wleOpReplaceEx(const TrackerHandle *parentHandle, TrackerHandle **handles, GroupDef **defs, Mat4 *mats, TrackerHandle ***newHandles)
{
	bool validated = true;
	GroupTracker *tracker;
	TrackerHandle *parentCopy;
	TrackerHandle **copiedHandles = NULL;
	TrackerHandle **createdHandles = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	int i, idx;
	bool origLocked = editState.lockedSelection;

	// validate: user is allowed to insert something into the parent
	if (!wleTrackerIsEditable(parentHandle, false, true, true))
		validated = false;

	for (i = 0; i < eaSize(&handles); i++)
	{
		assert(tracker = trackerFromTrackerHandle(handles[i]));

		// validate: none of the replaced trackers are layers
		if (!tracker->parent)
		{
			emStatusPrintf("Layer \"%s\" cannot be replaced!", tracker->def ? tracker->def->name_str : "DELETED");
			validated = false;
		}

		// validate: each of the replaced trackers' parents are modifiable
		if (!wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, true, true))
			validated = false;
	}

	if (!validated)
		return;

	// validate: trackers have same parent if EditInstances is enabled
	if (!wleEditInstancesValid(handles))
	{
		emStatusPrintf("When EditInstances is enabled, the replaced trackers cannot span across different groups!");
		return;
	}

	editState.lockedSelection = false;
	assert(parentHandle && (tracker = trackerFromTrackerHandle(parentHandle)) && tracker->def);
	parentCopy = trackerHandleCopy(parentHandle);
	assert(parentCopy);
	wleCopyHandles(&copiedHandles, handles);

	// start undo journal entry and group
	if (stack)
		EditUndoBeginGroup(stack);
	groupdbTransactionBegin();
	idx = wleFindCommonParentIndex(parentCopy, copiedHandles);

	// perform creation
	wleOpCreateMain(parentCopy, defs, mats, NULL, idx, &createdHandles);

	// delete the replaced trackers
	for(i = 0; i < eaSize(&copiedHandles); i++)
	{
		TrackerHandle *tempHandle;
		assert((tracker = trackerFromTrackerHandle(copiedHandles[i])) && tracker->parent);
		assert(tempHandle = trackerHandleCreate(tracker->parent));

		// instance as necessary and perform the deletion
		if (!groupIsPublic(tracker->parent->def) && !editState.editOriginal)
			groupdbInstance(tempHandle, true);
		groupdbDelete(copiedHandles[i]);
		trackerHandleDestroy(tempHandle);
	}
	wleOpCleanUniqueNames();
	entry = groupdbTransactionEnd();
	if (entry && stack)
		EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);

	// select new trackers
	wleTrackerSelectList(createdHandles, true);

	// end undo stack
	if (stack)
		EditUndoEndGroup(stack);

	// handle UI changes
	wleOpRefreshUI();
	wleUISearchClearUniqueness();
	emStatusPrintf("Replaced selection.");

	editState.lockedSelection = origLocked;
	if (newHandles)
	{
		eaPushEArray(newHandles, &createdHandles);
		eaDestroy(&createdHandles);
	}
	else
		eaDestroyEx(&createdHandles, trackerHandleDestroy);
	trackerHandleDestroy(parentCopy);
	eaDestroyEx(&copiedHandles, trackerHandleDestroy);
}

void wleOpReplaceEach(const TrackerHandle **parentHandles, TrackerHandle **handles, GroupDef **defs, Mat4 **mats, TrackerHandle ***newHandles, bool performSelection)
{
	GroupTracker *tracker, **trackers = NULL;
	TrackerHandle *parentCopy;
	TrackerHandle **copiedHandles = NULL;
	TrackerHandle **deleteHandles = NULL;
	TrackerHandle **createdHandles = NULL;
	int i, idx, *indices = NULL;
	bool origLocked = editState.lockedSelection;

	for (i = 0; i < eaSize(&handles); i++)
	{
		// validate: user is allowed to insert something into the parent
		if (!wleTrackerIsEditable(parentHandles[i], false, true, true))
		{
			continue;
		}

		assert(tracker = trackerFromTrackerHandle(handles[i]));

		// validate: none of the replaced trackers are layers
		if (!tracker->parent)
		{
			emStatusPrintf("Layer \"%s\" cannot be replaced!", tracker->def ? tracker->def->name_str : "DELETED");
			continue;
		}

		// validate: each of the replaced trackers' parents are modifiable
		if (!wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, true, true))
		{
			continue;
		}

		editState.lockedSelection = false;
		assert(parentHandles[i] && (tracker = trackerFromTrackerHandle(parentHandles[i])) && tracker->def);
		parentCopy = trackerHandleCopy(parentHandles[i]);
		assert(parentCopy);
		eaPush(&trackers, tracker);
		eaPush(&copiedHandles, trackerHandleCopy(handles[i]));

		idx = wleFindCommonParentIndex(parentCopy, handles);
		eaiPush(&indices, idx);
	}

	// perform creation
	wleOpCreateList(parentHandles, defs, mats, NULL, indices, &createdHandles, performSelection);

	for (i = 0; i < eaSize(&trackers); i++)
	{
		// instance as necessary and perform the deletion
		if ((trackers[i]->parent) && (!groupIsPublic(trackers[i]->parent->def) && !editState.editOriginal))
		{
			groupdbInstance(parentHandles[i], true);
		}
	}
	eaiDestroy(&indices);
	wleTrackerDeselectAll();
	groupdbDeleteList(copiedHandles);
	eaDestroy(&copiedHandles);
	wleOpCleanUniqueNames();

	// handle UI changes
	wleUISearchClearUniqueness();
	emStatusPrintf("Replaced selection.");

	editState.lockedSelection = origLocked;
	if (newHandles)
	{
		eaPushEArray(newHandles, &createdHandles);
		eaDestroy(&createdHandles);
	}
	else
	{
		eaDestroyEx(&createdHandles, trackerHandleDestroy);
	}
}

/******
* This is the main workhorse recursive function that does the find and replace.
* PARAMS:
*   root - TrackerHandle where find and replace will begin searching for matches
*   replacements - StashTable containing integer keys to integer data, representing the
*                  old UID and the new UID of the definition which will replace any occurrences
*                  of definitions matching the corresponding old UID.
******/
static void wleOpFindAndReplaceRecurse(const TrackerHandle *root, const StashTable replacements)
{
	GroupTracker *rootTracker = trackerFromTrackerHandle(root);
	int newUid;

	if (!rootTracker)
		return;

	if (rootTracker->def && rootTracker->parent && stashAddressFindInt(replacements, rootTracker->def, &newUid) && newUid)
	{
		TrackerHandle *handle = trackerHandleCreate(rootTracker);

		// perform replacement
		groupdbReplace(handle, newUid);

		// clean up
		trackerHandleDestroy(handle);
	}
	else if (rootTracker->def && !groupIsPublic(rootTracker->def))
	{
		int i;
		for (i = 0; i < rootTracker->child_count; i++)
		{
			TrackerHandle *childHandle = trackerHandleCreate(rootTracker->children[i]);
			wleOpFindAndReplaceRecurse(childHandle, replacements);
			trackerHandleDestroy(childHandle);
		}
	}
}

/******
* This function takes a hash table matching old object library UID's to new object library
* UID's and executes all such replacements on children of the specified root tracker; this will NOT
* recurse into object library pieces
* PARAMS:
*   root - TrackerHandle where find and replace will begin searching for matches; will NOT match root
*   replacements - StashTable containing address keys to integer data, representing the
*                  old GroupDef and the new UID of the definition which will replace any occurrences
*                  of definitions matching the corresponding old GroupDef.
******/
void wleOpFindAndReplace(const TrackerHandle *root, const StashTable replacements)
{
	bool validated = true;

	// validate: user is allowed to insert something into the parent
	if (!wleTrackerIsEditable(root, false, true, true))
		validated = false;

	if (validated)
	{
		GroupTracker *rootTracker = trackerFromTrackerHandle(root);
		EditUndoStack *stack = edObjGetUndoStack();
		JournalEntry *entry;
		int i;
		bool origLocked = editState.lockedSelection;

		if (!rootTracker || !rootTracker->child_count)
			return;

		editState.lockedSelection = false;
		if (stack)
			EditUndoBeginGroup(stack);
		wleTrackerDeselectAll();
		groupdbTransactionBegin();
		for (i = 0; i < rootTracker->child_count; i++)
		{
			TrackerHandle *childHandle = trackerHandleCreate(rootTracker->children[i]);
			wleOpFindAndReplaceRecurse(childHandle, replacements);
			trackerHandleDestroy(childHandle);
		}
		entry = groupdbTransactionEnd();
		if (stack)
		{
			if (entry)
				EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
			EditUndoEndGroup(stack);
		}

		// handle UI changes
		editState.lockedSelection = origLocked;
		wleOpRefreshUI();
		wleUISearchClearUniqueness();
	}
}

/******
* This function saves a map group to the object library.
* PARAMS:
*   handle - the TrackerHandle to the object to be saved
*   libPath - the relative path to the objlib file where the new piece will be saved
*   defName - the name of the new lib piece
******/

void wleOpSaveToLib(const TrackerHandle *handle, const char *libPath, const char *defName)
{
	GroupTracker *tracker;
	GroupDef *newDef;
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	char filename_abs[MAX_PATH], filename_rel[MAX_PATH];
	int uid;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	// validate: handle doesn't point to a layer
	if (!tracker->parent)
	{
		emStatusPrintf("Layer \"%s\" cannot be saved to the object library!", libPath);
		return;
	}

	// validate: handle doesn't point to an object library piece
	if (groupIsObjLib(tracker->def))
	{
		emStatusPrintf("Cannot resave object library piece \"%s\"!  Please instance \"%s\" into the layer first.", tracker->def->name_str, tracker->def->name_str);
		return;
	}

	// validate: source layer is locked
	if (!wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, true, true))
		return;
	
	// validate: destination file is in the data directory
	if (!fileIsInDataDirs(libPath))
	{
		emStatusPrintf("File \"%s\" does not reside in a data directory!", libPath);
		return;
	}

	// validate: destination file has "objlib" extension
	if (strcmpi(strrchr(libPath, '.') + 1, "objlib") != 0)
	{
		emStatusPrintf("File \"%s\" does not have the correct \".objlib\" extension!", libPath);
		return;
	}

	// Make sure the output path is relative, not absolute
	fileLocateWrite(libPath, filename_abs);
	fileRelativePath(filename_abs, filename_rel);

	// copy the GroupDef into the library file
	if (stack)
		EditUndoBeginGroup(stack);
	groupdbTransactionBegin();
	uid = groupdbTransfer(handle, filename_rel, defName);
	assert(uid < 0);
	newDef = objectLibraryGetEditingGroupDef(uid, false);
	assert(newDef);
	emStatusPrintf("Saved \"%s\" to the object library.", newDef->name_str);

	// replace all the GroupEnt references to the old GroupDef with the new GroupDef
	groupdbReplace(handle, uid);
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	wleOpRefreshUI();
	wleUIObjectTreeRefresh();
	wleUISearchClearUniqueness();

	if (!objectLibrarySave(newDef->filename))
	{
		emStatusPrintf("Failed to save object library piece \"%s\"!", tracker->def->name_str);
	}
}

/******
* This function deletes a group from the object library.
* PARAMS:
*   uid - int UID of the object library piece to be deleted from the library
******/
// TODO: have to check locks on all files that will be affected by deletion; add reporting and other
// validation as well
void wleOpDeleteFromLib(int uid)
{
	GroupDef *libDef;

	assert(uid < 0);
	libDef = objectLibraryGetGroupDef(uid, true);
	assert(libDef);

	//groupdbClearJournal();
	EditUndoStackClear(edObjGetUndoStack());
	groupdbDestroy(uid);

	// cleanup
	groupdbUpdateBounds(libDef->def_lib);
	wleOpRefreshUI();
	wleUIObjectTreeRefresh();
}

/******
* This function reseeds the selection.
* PARAMS:
*   handles - EArray of TrackerHandles to reseed
******/
void wleOpReseed(const TrackerHandle **handles)
{
	EditUndoStack *stack = edObjGetUndoStack();
	GroupTracker *tracker;
	JournalEntry *entry;
	bool validated = true;
	int i;

	// validate: something is being reseeded; otherwise, quietly return
	if (eaSize(&handles) == 0)
		return;

	for (i = 0; i < eaSize(&handles); i++)
	{
		assert(tracker = trackerFromTrackerHandle(handles[i]));

		// validate: none of the trackers are layers
		if (!tracker->parent)
		{
			emStatusPrintf("Layer \"%s\" cannot be moved!", tracker->def ? tracker->def->name_str : "DELETED");
			validated = false;
		}

		// validate: each of the trackers' parents are modifiable
		if (!wleTrackerIsEditable(trackerHandleFromTracker(tracker->parent), false, true, true))
			validated = false;
	}

	if (!validated)
		return;

	if (stack)
		EditUndoBeginGroup(stack);

	groupdbTransactionBegin();
	for (i = 0; i < eaSize(&handles); i++)
	{
		TrackerHandle *parentHandle;

		// instance parent if necessary
		assert((tracker = trackerFromTrackerHandle(handles[i])) && tracker->parent);
		assert(parentHandle = trackerHandleCreate(tracker->parent));
		if (!groupIsPublic(tracker->parent->def) && !editState.editOriginal)
			groupdbInstance(parentHandle, true);

		groupdbRandomize(handles[i]);
		trackerHandleDestroy(parentHandle);
	}
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// handle UI changes
	wleOpRefreshUI();
	return;
}


/******
* PROPERTIES-RELATED OPERATIONS AND FUNCTIONS
******/
static bool wleWithinPropOp = false;
static GroupDef *wlePropDef = NULL;

/******
* This function should be called before any property editing to a GroupDef is done.  This handles
* validation, instancing, journalling, and locking client-to-server updates.
* PARAMS:
*   handle - TrackerHandle of the tracker whose properties are being modified
******/
GroupTracker *wleOpPropsBegin(const TrackerHandle *handle)
{
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	EditUndoStack *stack = edObjGetUndoStack();

	// this should never be called while another property operation is occurring
	assert(!wleWithinPropOp);

	if (!tracker || !tracker->def)
		return NULL;

	ANALYSIS_ASSUME(handle);

	// validate: tracker is editable
	if (!wleTrackerIsEditable(handle, false, true, false))
		return NULL;

	// instance tracker if necessary
	wleWithinPropOp = true;
	if (stack)
		EditUndoBeginGroup(stack);
	groupdbTransactionBegin();
	if (!groupIsPublic(tracker->def) && !editState.editOriginal)
	{
		groupdbInstance(handle, true);
		tracker = trackerFromTrackerHandle(handle);
	}

	// remember which def is being modified
	assert(tracker && tracker->def);
	wlePropDef = tracker->def;

	// journal
	journalDef(tracker->def);
	groupdbDirtyDef(wlePropDef, UPDATE_GROUP_PROPERTIES);

	// lock client-to-server updates
	worldPauseLockedUpdates(true);

	return tracker;
}

/******
* This function should be called every time a property is changed while in a property-editing
* transaction.  This function reloads the affected tracker to ensure that the property change
* is reflected in the world.  Remember that this MUST be called within a wleOpPropsBegin/End block
******/
void wleOpPropsUpdate(void)
{
	// this should only ever be called within a property operation
	assert(wleWithinPropOp);

	groupdbDirtyDef(wlePropDef, UPDATE_GROUP_PROPERTIES);
	groupdbUpdateBounds(wlePropDef->def_lib);
	zmapTrackerUpdate(NULL, false, false);
	terrainUpdateTerrainObjectsByDef(wlePropDef);
	// TODO: remove this part when the tracker tree is handle based
	wleUITrackerTreeRefresh(NULL);
}

/******
* This function is called to end a property-editing transaction.  It finishes off the journal
* entry, and resumes client-to-server updates.
******/
void wleOpPropsEndNoUIUpdate(void)
{
	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;

	wleWithinPropOp = false;
	groupdbDirtyDef(wlePropDef, UPDATE_GROUP_PROPERTIES);
 	groupdbUpdateBounds(wlePropDef->def_lib);
	zmapTrackerUpdate(NULL, false, false);
	terrainUpdateTerrainObjectsByDef(wlePropDef);
	entry = groupdbTransactionEnd();
	if (stack)
	{
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}
	worldPauseLockedUpdates(false);
}

// above, and updates UI
void wleOpPropsEnd(void)
{
	wleOpPropsEndNoUIUpdate();
	wleOpRefreshUI();
}

/******
* ENCOUNTER OPERATIONS
******/
/******
* This operation sets a tracker's unique name within a particular scope, also fixing up the path referencing
* the old name in the parent scope (if it exists).
* PARAMS:
*   scopeHandle - TrackerHandle to the scope tracker
*   handle - TrackerHandle to the tracker being named
*   name - string unique name
* RETURNS:
*   bool indicating whether the operation succeeded
******/
bool wleOpSetUniqueScopeName(TrackerHandle *scopeHandle, TrackerHandle *handle, const char *name)
{
	TrackerHandle *scopeTrackerHandle;
	GroupTracker *scopeTracker = trackerFromTrackerHandle(scopeHandle);
	GroupTracker *scopeTrackerCopy;
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	EditUndoStack *stack = edObjGetUndoStack();
	LogicalGroup *parentGroup = NULL;
	JournalEntry *entry;
	bool valid = true;
	const char *oldName = NULL;
	const char *uniqueName = NULL;

	// validate: ensure unique name is valid
	if (strncmp(GROUP_UNNAMED_PREFIX, name, strlen(GROUP_UNNAMED_PREFIX)) == 0)
	{
		emStatusPrintf("Must specify a new name without the unnamed prefix.");
		return false;
	}

	// validate: make sure current scope and next scope upward are both editable
	if (!tracker)
		return false;

	if (!scopeTracker)
		scopeTracker = layerGetTracker(tracker->parent_layer);
	assert(scopeTracker);
	scopeTrackerCopy = scopeTracker;
	scopeTrackerHandle = trackerHandleCreate(scopeTracker);
	assert(scopeTrackerHandle);

	if (!wleTrackerIsEditable(scopeTrackerHandle, false, true, true))
		valid = false;
	scopeTrackerCopy = trackerGetScopeTracker(scopeTracker);
	if (scopeTrackerCopy && scopeTrackerCopy->def)
	{
		TrackerHandle *checkHandle = trackerHandleCreate(scopeTrackerCopy);
		if (!wleTrackerIsEditable(checkHandle, false, true, true))
			valid = false;
		trackerHandleDestroy(checkHandle);
	}

	// validate: new name is different from old name
	oldName = trackerGetUniqueScopeName(scopeTracker->def, tracker, NULL);
	if (valid && oldName && strcmp(oldName, name) == 0)
		valid = false;

	// validate: new name is unique
	if (valid && !groupDefScopeIsNameUnique(scopeTracker->def, name))
	{
		emStatusPrintf("The name \"%s\" is already being used by another object in the same scope.", name);
		valid = false;
	}

	// validate: ensure path to the tracker won't reference a temporary name
	if (valid && !wleIsUniqueNameable(scopeTrackerHandle, handle, NULL))
	{
		emStatusPrintf("Tracker \"%s\" first needs to have a permanent name specified at all subscopes.", tracker->def->name_str);
		valid = false;
	}

	// validate: tracker is a descendant of scopeTracker
	if (!wleTrackerIsDescendant(handle, scopeTrackerHandle))
	{
		emStatusPrintf("Cannot set unique name on a scope for an object not inside of the scope.");
		valid = false;
	}

	if (!valid)
	{
		trackerHandleDestroy(scopeTrackerHandle);
		return false;
	}

	// start transaction
	if (stack)
	{
		EditUndoBeginGroup(stack);
		groupdbTransactionBegin();
	}

	// get the parent logical group, if applicable
	if (scopeTracker->def && scopeTracker->closest_scope && tracker->enc_obj && tracker->enc_obj->parent_group &&
		tracker->enc_obj->parent_group->common_data.closest_scope == scopeTracker->closest_scope)
		parentGroup = wleFindLogicalGroup(scopeTracker->def, scopeTracker->closest_scope, tracker->enc_obj->parent_group);

	// set name in closest scope
	if (scopeTrackerCopy)
		uniqueName = trackerGetUniqueScopeName(scopeTrackerCopy->def, trackerFromTrackerHandle(handle), NULL);
	groupdbSetUniqueScopeName(scopeTracker->def, handle, NULL, name, !uniqueName);

	// fix up path in scope above the current one to point to the same name
	if (uniqueName)
		groupdbSetUniqueScopeName(scopeTrackerCopy->def, handle, NULL, uniqueName, true);

	// fix up parent logical group
	if (parentGroup && oldName)
	{
		int i;
		for (i = 0; i < eaSize(&parentGroup->child_names); i++)
		{
			if (strcmpi(parentGroup->child_names[i], oldName) == 0)
			{
				journalDef(scopeTracker->def);
				groupdbDirtyDef(scopeTracker->def, -1);
				StructFreeString(parentGroup->child_names[i]);
				parentGroup->child_names[i] = StructAllocString(name);
				break;
			}
		}
	}

	// clean up unused paths
	wleOpCleanUniqueNames();

	// end transaction and reopen trackers
	if (stack)
	{
		entry = groupdbTransactionEnd();
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}
	wleOpRefreshUI();
	trackerHandleDestroy(scopeTrackerHandle);

	return true;
}

void wleOpSetUniqueZoneMapScopeName(TrackerHandle *handle, const char *name)
{
	WorldScope *scope;
	GroupTracker *tracker;
	EditUndoStack *stack = edObjGetUndoStack();
	int *uids = NULL;
	char *oldName = NULL;
	char *uniqueName = NULL;
	bool valid = true, editable = true, firstPath = true;
	int i;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);
	scope = tracker->closest_scope;

	// If this is the root of a library piece, then apply the name in the parent
	// scope instead of the library piece.
	if (tracker->parent && (tracker->parent->closest_scope != scope)) {
		scope = tracker->parent->closest_scope;
	}

	// Error but accept the name anyway for now
	if (!resIsValidName(name))
		Errorf("The scope name '%s' is not valid.", name);

	// validate uniqueness of name and editability at each scope level
	while (valid && scope)
	{
		TrackerHandle *checkHandle = NULL;

		// zonemap scope level
		if (!scope->parent_scope)
		{
			GroupTracker *layerTracker = layerGetTracker(tracker->parent_layer);

			checkHandle = trackerHandleCreate(layerGetTracker(tracker->parent_layer));

			// checking editability
			if (!wleTrackerIsEditable(checkHandle, false, true, true))
				editable = false;
			// checking scope name uniqueness
			else if (layerTracker && !groupDefScopeIsNameUnique(layerTracker->def, name) && trackerFromUniqueName(layerTracker, name) != tracker)
			{
				emStatusPrintf("The name \"%s\" is already used in the map scope.", name);
				valid = false;
			}
		}
		// library scope level
		else
		{
			checkHandle = trackerHandleCreate(scope->tracker);

			// checking editability
			if (!wleTrackerIsEditable(checkHandle, false, true, true))
				editable = false;
			// checking scope name uniqueness
			if (!groupDefScopeIsNameUnique(scope->tracker->def, name) && trackerFromUniqueName(scope->tracker, name) != tracker)
			{
				emStatusPrintf("The name \"%s\" is already used in scope defined at \"%s\".", name, scope->tracker->def->name_str);
				valid = false;
			}
		}

		scope = scope->parent_scope;
		if (checkHandle)
			trackerHandleDestroy(checkHandle);
	}

	if (!valid || !editable)
		return;

	if (stack)
		EditUndoBeginGroup(stack);

	while (tracker)
	{
		TrackerHandle *tempHandle = trackerHandleCreate(tracker);

		// for scope defs
		if (tracker->def &&
			((tracker->closest_scope->def && tracker->def == tracker->closest_scope->def) ||
			(!tracker->closest_scope->def && !tracker->parent)))
		{
			if (eaiSize(&uids) > 0)
			{
				char *path = NULL;

				eaiReverse(&uids);
				for (i = 0; i < eaiSize(&uids); i++)
					estrConcatf(&path, "%i,", uids[i]);
				if (uniqueName)
					estrConcatf(&path, "%s,", uniqueName);

				wleOpPropsBegin(tempHandle);

				// remove path entry under old name
				tracker = trackerFromTrackerHandle(tempHandle);
				SAFE_FREE(uniqueName);
				if (stashRemovePointer(tracker->def->path_to_name, path, &oldName))
				{
					uniqueName = strdup(oldName);
					stashRemovePointer(tracker->def->name_to_path, oldName, NULL);
				}

				// alter path to use new name
				if (!firstPath)
				{
					estrTruncateAtLastOccurrence(&path, ',');
					estrTruncateAtLastOccurrence(&path, ',');
					estrConcatf(&path, ",%s,", name);
				}

				// add under new name (if any) to both stash tables
				assert(name);
				groupDefScopeSetPathName(tracker->def, path, name, false);
				wleOpPropsEnd();
				estrDestroy(&path);
				firstPath = false;
			}

			eaiClear(&uids);
		}

		tracker = trackerFromTrackerHandle(tempHandle);
		eaiPush(&uids, tracker->uid_in_parent);
		tracker = tracker->parent;
		trackerHandleDestroy(tempHandle);
	}
	if (stack)
		EditUndoEndGroup(stack);

	SAFE_FREE(uniqueName);
	eaiDestroy(&uids);
}

static bool wleOpLogicalGroupIsDescendant(WorldEncounterObject *parent, WorldEncounterObject *child)
{
	if (!parent || !child)
		return false;
	else if (parent == child)
		return true;
	else if (parent->type != WL_ENC_LOGICAL_GROUP)
		return false;
	else
	{
		do
		{
			child = (WorldEncounterObject*) child->parent_group;
			if (child == parent)
				return true;
		} while (child);
	}

	return false;
}

bool wleOpLogicalGroup(TrackerHandle *scopeHandle, const char *groupName, const char **uniqueNames, bool validateOnly)
{
	ZoneMapLayer *layer = NULL;
	TrackerHandle *scopeHandleCopy = trackerHandleCopy(scopeHandle);
	GroupTracker *scopeTracker = trackerFromTrackerHandle(scopeHandleCopy);
	WorldScope *closestScope = scopeTracker ? scopeTracker->closest_scope : (WorldScope*) zmapGetScope(NULL);
	EditUndoStack *stack = edObjGetUndoStack();
	WorldLogicalGroup *commonParent = NULL;
	LogicalGroup *newGroup;
	WorldEncounterObject **objects = NULL;
	bool valid = true;
	int i;

	// validate: unique names are defined in the scope, belong to the same layer, and are not temporary names
	for (i = 0; valid && i < eaSize(&uniqueNames); i++)
	{
		WorldEncounterObject *object;

		if (!uniqueNames[i] || !uniqueNames[i][0])
		{
			emStatusPrintf("Cannot add objects without a unique name to a logical group.");
			valid = false;
		}
		else if (strncmp(GROUP_UNNAMED_PREFIX, uniqueNames[i], strlen(GROUP_UNNAMED_PREFIX)) == 0)
		{
			emStatusPrintf("Give \"%s\" a permanent name before grouping.", uniqueNames[i]);
			valid = false;
		}
		else if (!stashFindPointer(closestScope->name_to_obj, uniqueNames[i], &object))
		{
			emStatusPrintf("Object with unique name \"%s\" is not within the active scope.", uniqueNames[i]);
			valid = false;
		}
		else if (i && object->layer != layer)
		{
			emStatusPrintf("Grouped objects must all belong to the same layer.");
			valid = false;
		}
		else
		{
			if (!i)
				layer = object->layer;
			eaPush(&objects, object);
		}
	}

	// validate: at least one object to group
	if (valid && eaSize(&objects) == 0)
		valid = false;

	// validate: objects are not descendants of each other (meanwhile, find common parent)
	if (valid)
	{
		int j;

		for (i = 0; valid && i < eaSize(&objects); i++)
		{
			// check to see whether this object is a descendant of another object
			for (j = 0; valid && j < eaSize(&objects); j++)
			{
				if (i != j && wleOpLogicalGroupIsDescendant(objects[j], objects[i]))
					valid = false;
			}

			// determine current common parent
			if (valid)
			{
				if (!i)
					commonParent = objects[i]->parent_group;
				else
				{
					while (commonParent && !wleOpLogicalGroupIsDescendant((WorldEncounterObject*) commonParent, objects[i]))
						commonParent = commonParent->common_data.parent_group;
				}
			}
		}
	}

	assert(!valid || layer);
	if (valid && !scopeTracker)
	{
		scopeTracker = layerGetTracker(layer);
		scopeHandleCopy = trackerHandleCreate(scopeTracker);
	}

	// validate: scope tracker exists, has a def, and is valid
	if (valid && (!scopeTracker || !scopeTracker->def || !closestScope))
	{
		emStatusPrintf("Scope tracker is invalid.");
		valid = false;
	}

	// validate: scope def is editable
	if (valid && !wleTrackerIsEditable(scopeHandleCopy, false, true, false))
		valid = false;

	// validate: group name is unique
	if (valid && ((groupName && !groupDefScopeIsNameUnique(scopeTracker->def, groupName)) || (!groupName && !validateOnly)))
	{
		emStatusPrintf("The name \"%s\" is already being used by another object in the same scope.", groupName);
		valid = false;
	}

	if (!valid)
	{
		eaDestroy(&objects);
		trackerHandleDestroy(scopeHandleCopy);
		return false;
	}
	if (validateOnly)
		return true;

	// start transaction
	if (stack)
	{
		EditUndoBeginGroup(stack);
		groupdbTransactionBegin();
	}

	journalDef(scopeTracker->def);
	groupdbDirtyDef(scopeTracker->def, -1);

	// create and add logical group
	newGroup = StructCreate(parse_LogicalGroup);
	newGroup->group_name = StructAllocString(groupName);
	for (i = 0; i < eaSize(&uniqueNames); i++)
		eaPush(&newGroup->child_names, StructAllocString(uniqueNames[i]));
	eaPush(&scopeTracker->def->logical_groups, newGroup);

	// remove children from old parent groups
	assert(eaSize(&objects) == eaSize(&uniqueNames));
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->parent_group && stashFindPointer(closestScope->obj_to_name, objects[i]->parent_group, NULL))
		{
			LogicalGroup *logicalGroup = wleFindLogicalGroup(scopeTracker->def, closestScope, objects[i]->parent_group);
			int j;

			assert(logicalGroup);
			for (j = 0; j < eaSize(&logicalGroup->child_names); j++)
			{
				if (strcmpi(logicalGroup->child_names[j], uniqueNames[i]) == 0)
				{
					StructFreeString(logicalGroup->child_names[j]);
					eaRemove(&logicalGroup->child_names, j);
					break;
				}
			}
		}
	}

	// parent the new group
	if (commonParent && stashFindPointer(closestScope->obj_to_name, commonParent, NULL))
	{
		LogicalGroup *parentGroup = wleFindLogicalGroup(scopeTracker->def, closestScope, commonParent);
		assert(parentGroup);
		eaPush(&parentGroup->child_names, StructAllocString(newGroup->group_name));
	}

	zmapTrackerUpdate(NULL, false, false);

	// end transaction
	if (stack)
	{
		JournalEntry *entry = groupdbTransactionEnd();
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// refresh UI
	wleOpRefreshUI();

	// cleanup
	eaDestroy(&objects);
	trackerHandleDestroy(scopeHandleCopy);

	return true;
}

void wleOpAddToLogicalGroup(TrackerHandle *scopeHandle, const char *parentName, const char **uniqueNames, int index)
{
	TrackerHandle *scopeHandleCopy = trackerHandleCopy(scopeHandle);
	GroupTracker *scopeTracker = trackerFromTrackerHandle(scopeHandleCopy);
	WorldScope *closestScope = scopeTracker ? scopeTracker->closest_scope : (WorldScope*) zmapGetScope(NULL);
	ZoneMapLayer *layer = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	WorldEncounterObject *parentGroup = NULL;
	WorldEncounterObject **objects = NULL;
	LogicalGroup *lg;
	bool valid = true;
	int i;

	// validate: unique names are defined in the scope and belong to the same layer
	assert(closestScope);
	if (!parentName || !parentName[0])
	{
		emStatusPrintf("Invalid destination group.");
		valid = false;
	}
	else if (!stashFindPointer(closestScope->name_to_obj, parentName, &parentGroup) || parentGroup->type != WL_ENC_LOGICAL_GROUP)
	{
		emStatusPrintf("Logical group \"%s\" is not within the active scope.", parentName);
		valid = false;
	}
	else if (parentGroup->closest_scope != closestScope)
	{
		emStatusPrintf("Logical group \"%s\" is defined at a subscope; switch active scopes to edit the group.", parentName);
		valid = false;
	}
	if (valid)
		layer = parentGroup->layer;
	for (i = 0; valid && i < eaSize(&uniqueNames); i++)
	{
		WorldEncounterObject *object;

		if (!uniqueNames[i] || !uniqueNames[i][0])
		{
			emStatusPrintf("Cannot add objects without a unique name to a logical group.");
			valid = false;
		}
		else if (strncmp(GROUP_UNNAMED_PREFIX, uniqueNames[i], strlen(GROUP_UNNAMED_PREFIX)) == 0)
		{
			emStatusPrintf("Give \"%s\" a permanent name before adding it to a group.", uniqueNames[i]);
			valid = false;
		}
		else if (!stashFindPointer(closestScope->name_to_obj, uniqueNames[i], &object))
		{
			emStatusPrintf("Object with unique name \"%s\" is not within the active scope.", uniqueNames[i]);
			valid = false;
		}
		else if (object->parent_group && object->parent_group->common_data.closest_scope != closestScope)
		{
			emStatusPrintf("Cannot remove \"%s\" from its parent group; parent is defined at a different scope.", uniqueNames[i]);
			valid = false;
		}
		else if (object->layer != layer)
		{
			emStatusPrintf("Grouped objects must all belong to the parent group's layer");
			valid = false;
		}
		else
			eaPush(&objects, object);
	}

	// validate: at least one object to add to the group
	if (valid && eaSize(&objects) == 0)
		valid = false;

	// validate: objects are not descendants of each other (meanwhile, find common parent)
	if (valid)
	{
		int j;

		for (i = 0; valid && i < eaSize(&objects); i++)
		{
			// check to see whether this object is a descendant of another object
			for (j = 0; valid && j < eaSize(&objects); j++)
			{
				if (i != j && wleOpLogicalGroupIsDescendant(objects[j], objects[i]))
					valid = false;
			}
		}
	}

	assert(!valid || layer);
	if (valid && !scopeTracker)
	{
		scopeTracker = layerGetTracker(layer);
		scopeHandleCopy = trackerHandleCreate(scopeTracker);
	}

	// validate: scope tracker exists, has a def, and is valid
	if (valid && (!scopeTracker || !scopeTracker->def || !closestScope))
	{
		emStatusPrintf("Scope tracker is invalid.");
		valid = false;
	}

	// validate: scope def is editable
	if (valid && !wleTrackerIsEditable(scopeHandleCopy, false, true, false))
		valid = false;

	if (!valid)
	{
		eaDestroy(&objects);
		trackerHandleDestroy(scopeHandleCopy);
		return;
	}

	// start transaction
	if (stack)
	{
		EditUndoBeginGroup(stack);
		groupdbTransactionBegin();
	}

	journalDef(scopeTracker->def);
	groupdbDirtyDef(scopeTracker->def, -1);

	// remove children from old parent groups
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->parent_group)
		{
			LogicalGroup *logicalGroup = wleFindLogicalGroup(scopeTracker->def, closestScope, objects[i]->parent_group);
			int j;

			assert(logicalGroup);
			for (j = 0; j < eaSize(&logicalGroup->child_names); j++)
			{
				if (strcmpi(logicalGroup->child_names[j], uniqueNames[i]) == 0)
				{
					StructFreeString(logicalGroup->child_names[j]);
					eaRemove(&logicalGroup->child_names, j);
					break;
				}
			}
		}
	}

	// add children to new parent group
	assert(eaSize(&objects) == eaSize(&uniqueNames));
	lg = wleFindLogicalGroup(scopeTracker->def, closestScope, (WorldLogicalGroup*) parentGroup);
	for (i = 0; i < eaSize(&uniqueNames); i++)
		eaInsert(&lg->child_names, StructAllocString(uniqueNames[i]), index);

	zmapTrackerUpdate(NULL, false, false);

	// end transaction
	if (stack)
	{
		JournalEntry *entry = groupdbTransactionEnd();
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// refresh UI
	wleOpRefreshUI();

	// cleanup
	eaDestroy(&objects);
	trackerHandleDestroy(scopeHandleCopy);
}

void wleOpLogicalUngroup(TrackerHandle *scopeHandle, const char *uniqueName)
{
	TrackerHandle *scopeHandleCopy = trackerHandleCopy(scopeHandle);
	GroupTracker *scopeTracker = trackerFromTrackerHandle(scopeHandleCopy);
	WorldScope *closestScope = scopeTracker ? scopeTracker->closest_scope : (WorldScope*) zmapGetScope(NULL);
	EditUndoStack *stack = edObjGetUndoStack();
	WorldLogicalGroup *group = NULL;
	LogicalGroup *lg, *parentLg;
	bool valid = true;
	int i;

	// validate: unique name is defined in this scope
	assert(closestScope);
	if (!uniqueName || !uniqueName[0])
	{
		emStatusPrintf("Cannot ungroup objects without a unique name");
		valid = false;
	}
	else if (!stashFindPointer(closestScope->name_to_obj, uniqueName, &group) || group->common_data.type != WL_ENC_LOGICAL_GROUP)
	{
		emStatusPrintf("Logical group with unique name \"%s\" is not within the active scope.", uniqueName);
		valid = false;
	}
	else if (group->common_data.closest_scope != closestScope)
	{
		emStatusPrintf("Logical group \"%s\" is defined at a subscope; switch active scopes to edit the group.", uniqueName);
		valid = false;
	}

	assert(group);
	if (!scopeTracker)
	{
		scopeTracker = layerGetTracker(group->common_data.layer);
		scopeHandleCopy = trackerHandleCreate(scopeTracker);
	}

	// validate: scope tracker exists, has a def, and is valid
	if (valid && (!scopeTracker || !scopeTracker->def || !closestScope))
	{
		emStatusPrintf("Scope tracker is invalid.");
		valid = false;
	}

	// validate: scope def is editable
	if (!wleTrackerIsEditable(scopeHandleCopy, false, true, false))
		valid = false;

	if (!valid)
	{
		trackerHandleDestroy(scopeHandleCopy);
		return;
	}

	// start transaction
	if (stack)
	{
		EditUndoBeginGroup(stack);
		groupdbTransactionBegin();
	}

	journalDef(scopeTracker->def);
	groupdbDirtyDef(scopeTracker->def, -1);

	// remove group from its parent (if the parent exists in the scope)
	lg = wleFindLogicalGroup(scopeTracker->def, closestScope, group);
	if (group->common_data.parent_group && (parentLg = wleFindLogicalGroup(scopeTracker->def, closestScope, group->common_data.parent_group)))
	{
		int insertIndex;
		for (insertIndex = eaSize(&parentLg->child_names) - 1; insertIndex >= 0; insertIndex--)
		{
			if (strcmpi(parentLg->child_names[insertIndex], uniqueName) == 0)
			{
				StructFreeString(eaRemove(&parentLg->child_names, insertIndex));
				break;
			}
		}

		// insert children into parent group
		for (i = 0; i < eaSize(&lg->child_names); i++)
			eaInsert(&parentLg->child_names, StructAllocString(lg->child_names[i]), insertIndex++);
	}

	// destroy the logical group being ungrouped
	eaFindAndRemove(&scopeTracker->def->logical_groups, lg);
	StructDestroy(parse_LogicalGroup, lg);

	// update the data
	zmapTrackerUpdate(NULL, false, false);

	// end transaction
	if (stack)
	{
		JournalEntry *entry = groupdbTransactionEnd();
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}

	// refresh UI
	wleOpRefreshUI();

	// cleanup
	trackerHandleDestroy(scopeHandleCopy);
}

bool wleOpSetLogicalGroupUniqueScopeName(TrackerHandle *scopeHandle, const char *oldName, const char *newName)
{
	WorldScope *zmapScope = (WorldScope*) zmapGetScope(NULL);
	TrackerHandle *scopeTrackerHandle = NULL;
	GroupTracker *scopeTracker = trackerFromTrackerHandle(scopeHandle);
	GroupTracker *scopeTrackerCopy;
	WorldLogicalGroup *logicalGroup = NULL;
	LogicalGroup *parentGroup = NULL;
	TrackerHandle *handle;

	EditUndoStack *stack = edObjGetUndoStack();
	JournalEntry *entry;
	bool valid = true;
	const char *closestScopeName = NULL;
	char *uniqueName = NULL;

	// validate: zonemap scope exists
	if (!zmapScope)
	{
		emStatusPrintf("Zonemap scope does not exist!  Grab a programmer!");
		valid = false;
	}

	// validate: logical group exists
	if (valid)
	{
		logicalGroup = (WorldLogicalGroup*) worldScopeGetObject(scopeTracker ? scopeTracker->closest_scope : zmapScope, oldName);
		if (!logicalGroup)
		{
			emStatusPrintf("No logical group by the name of \"%s\" exists in the active scope.", oldName);
			valid = false;
		}
	}

	// validate: old name and new name are different
	if (strcmp(oldName, newName) == 0)
		valid = false;
	
	// validate: make sure current scope and next scope upward are both editable
	if (!scopeTracker && logicalGroup)
		scopeTracker = layerGetTracker(logicalGroup->common_data.layer);
	assert(!valid || scopeTracker);
	scopeTrackerCopy = scopeTracker;
	scopeTrackerHandle = trackerHandleCreate(scopeTracker);
	assert(!valid || scopeTrackerHandle);

	if (valid && !wleTrackerIsEditable(scopeTrackerHandle, false, true, true))
		valid = false;
	scopeTrackerCopy = trackerGetScopeTracker(scopeTracker);
	if (valid && scopeTrackerCopy && scopeTrackerCopy->def)
	{
		TrackerHandle *checkHandle = trackerHandleCreate(scopeTrackerCopy);
		if (!wleTrackerIsEditable(checkHandle, false, true, true))
			valid = false;
		trackerHandleDestroy(checkHandle);
	}

	// validate: new name is unique
	if (valid && !groupDefScopeIsNameUnique(scopeTracker->def, newName))
	{
		emStatusPrintf("The name \"%s\" is already being used by another object in the same scope.", newName);
		valid = false;
	}

	if (!valid)
	{
		trackerHandleDestroy(scopeTrackerHandle);
		return false;
	}

	// start transaction
	if (stack)
	{
		EditUndoBeginGroup(stack);
		groupdbTransactionBegin();
	}

	// get the parent logical group, if applicable
	if (logicalGroup->common_data.parent_group && logicalGroup->common_data.parent_group->common_data.closest_scope == scopeTracker->closest_scope)
		parentGroup = wleFindLogicalGroup(scopeTracker->def, scopeTracker->closest_scope, logicalGroup->common_data.parent_group);

	// set name in scope
	handle = trackerHandleCreate(logicalGroup->common_data.tracker);
	assert(handle);
	if (scopeTrackerCopy)
		uniqueName = (char*) trackerGetUniqueScopeName(scopeTrackerCopy->def, trackerFromTrackerHandle(handle), oldName);
	groupdbSetUniqueScopeName(scopeTracker->def, handle, worldScopeGetObjectName(logicalGroup->common_data.closest_scope, (WorldEncounterObject*) logicalGroup), newName, !uniqueName);

	// fix up path in scope above the current one to point to the same name
	if (uniqueName)
		groupdbSetUniqueScopeName(scopeTrackerCopy->def, handle, worldScopeGetObjectName(logicalGroup->common_data.closest_scope, (WorldEncounterObject*) logicalGroup), uniqueName, true);	

	// fix up parent logical group
	if (parentGroup)
	{
		int i;
		for (i = 0; i < eaSize(&parentGroup->child_names); i++)
		{
			if (strcmpi(parentGroup->child_names[i], oldName) == 0)
			{
				journalDef(scopeTracker->def);
				groupdbDirtyDef(scopeTracker->def, -1);
				StructFreeString(parentGroup->child_names[i]);
				parentGroup->child_names[i] = StructAllocString(newName);
				break;
			}
		}
	}

	// clean up unused paths
	wleOpCleanUniqueNames();

	// end transaction and reopen trackers
	if (stack)
	{
		entry = groupdbTransactionEnd();
		if (entry)
			EditCreateUndoCustom(stack, wleOpUndoFunction, wleOpUndoFunction, wleOpFreeFunction, entry);
		EditUndoEndGroup(stack);
	}
	wleOpRefreshUI();
	trackerHandleDestroy(scopeTrackerHandle);

	return true;
}

static int wleOpSubHandleCmp(const WleEncObjSubHandle **subHandle1, const WleEncObjSubHandle **subHandle2)
{
	return (*subHandle2)->childIdx - (*subHandle1)->childIdx;
}

int wleOpAddPatrolPoint(const TrackerHandle *patrol, int index, Vec3 worldPos)
{
	EditUndoStack *stack = edObjGetUndoStack();
	WorldPatrolProperties *patrolProps = NULL;
	WorldPatrolPointProperties *patrolPoint;
	GroupTracker *tracker = trackerFromTrackerHandle(patrol);
	Mat4 tempMat, invMat;
	bool validated = true;
	int retIdx = -1;

	// validate: user is allowed to edit patrol
	if (!wleTrackerIsEditable(patrol, false, true, true))
		validated = false;

	if (!validated)
		return retIdx;

	tracker = wleOpPropsBegin(patrol);
	assert(tracker && tracker->def && (patrolProps = tracker->def->property_structs.patrol_properties));

	wleTrackerDeselectAll();

	patrolPoint = StructCreate(parse_WorldPatrolPointProperties);
	trackerGetMat(tracker, tempMat);
	invertMat4(tempMat, invMat);
	mulVecMat4(worldPos, invMat, patrolPoint->pos);
	identityMat3(patrolPoint->draw_mat);
	copyVec3(patrolPoint->pos, patrolPoint->draw_mat[3]);
	if (index < 0)
		eaPush(&patrolProps->patrol_points, patrolPoint);
	else
		eaInsert(&patrolProps->patrol_points, patrolPoint, index);

	// select the new patrol point
	retIdx = index < 0 ? eaSize(&patrolProps->patrol_points) - 1 : index;
	wlePatrolPointSelect(patrol, retIdx, true, true);
	wleOpPropsEnd();

	return retIdx;
}

bool wleOpMovePatrolPoints(const WleEncObjSubHandle **points, Vec3 *pos)
{
	EditUndoStack *stack = edObjGetUndoStack();
	Mat4 tempMat, invMat;
	bool validated = true;
	int i;

	// validate: something is being moved; otherwise, quietly return
	if (eaSize(&points) == 0)
		return false;

	for (i = 0; i < eaSize(&points); i++)
	{
		// validate: each of the trackers are modifiable
		if (!wleTrackerIsEditable(points[i]->parentHandle, false, true, true))
			validated = false;
	}

	if (!validated)
		return false;

	if (stack)
		EditUndoBeginGroup(stack);

	for (i = 0; i < eaSize(&points); i++)
	{
		TrackerHandle *parentHandle = points[i]->parentHandle;
		GroupTracker *parentTracker;
		WorldPatrolPointProperties *patrolPoint = NULL;
		
		parentTracker = wleOpPropsBegin(parentHandle);
		if (parentTracker && parentTracker->def && parentTracker->def->property_structs.patrol_properties && points[i]->childIdx >= 0 && points[i]->childIdx < eaSize(&parentTracker->def->property_structs.patrol_properties->patrol_points))
			patrolPoint = parentTracker->def->property_structs.patrol_properties->patrol_points[points[i]->childIdx];
		assert(patrolPoint);

		// move points
		trackerGetMat(parentTracker, tempMat);
		invertMat4(tempMat, invMat);
		mulVecMat4(pos[i], invMat, patrolPoint->pos);

		wleOpPropsEnd();
	}
	if (stack)
		EditUndoEndGroup(stack);

	// handle UI changes
	wleOpRefreshUI();
	return true;
}

bool wleOpMovePatrolPointToIndex(const WleEncObjSubHandle *point, int index)
{
	EditUndoStack *stack = edObjGetUndoStack();
	TrackerHandle *parentHandle = point->parentHandle;
	GroupTracker *parentTracker;
	WorldPatrolPointProperties *patrolPoint = NULL;
	ZoneMapLayer *parentLayer;
	bool validated = true;

	// validate: point's parent tracker is modifiable
	if (!wleTrackerIsEditable(parentHandle, false, true, true))
		validated = false;

	if (!validated)
		return false;

	if (stack)
		EditUndoBeginGroup(stack);

	edObjSelectionClear(EDTYPE_PATROL_POINT);
	parentTracker = wleOpPropsBegin(parentHandle);

	// move the point to appropriate index
	assert(parentTracker && parentTracker->def && parentTracker->def->property_structs.patrol_properties && point->childIdx >= 0 && point->childIdx < eaSize(&parentTracker->def->property_structs.patrol_properties->patrol_points));
	parentLayer = parentTracker->parent_layer;
	eaMove(&parentTracker->def->property_structs.patrol_properties->patrol_points, index, point->childIdx); 
	wleOpPropsEnd();

	wlePatrolPointSelect(parentHandle, index, true, false);
	if (stack)
		EditUndoEndGroup(stack);

	// handle UI changes
	wleOpRefreshUI();
	return true;
}

bool wleOpDeletePatrolPointsEx(const WleEncObjSubHandle **points, bool verbose)
{
	const WleEncObjSubHandle **pointsCopy = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	bool validated = true;
	int i;

	// validate: something is being deleted; otherwise, quietly return
	if (eaSize(&points) == 0)
		return false;

	for (i = 0; i < eaSize(&points); i++)
	{
		// validate: each of the trackers are modifiable
		if (!wleTrackerIsEditable(points[i]->parentHandle, false, true, true))
			validated = false;
	}

	if (!validated)
		return false;

	eaCopy(&pointsCopy, &points);
	eaQSort(pointsCopy, wleOpSubHandleCmp);
	if (stack)
		EditUndoBeginGroup(stack);

	edObjSelectionClear(EDTYPE_PATROL_POINT);
	for (i = 0; i < eaSize(&pointsCopy); i++)
	{
		TrackerHandle *parentHandle = pointsCopy[i]->parentHandle;
		GroupTracker *parentTracker;

		parentTracker = wleOpPropsBegin(parentHandle);
		assert(parentTracker && parentTracker->def && parentTracker->def->property_structs.patrol_properties && pointsCopy[i]->childIdx >= 0 && pointsCopy[i]->childIdx < eaSize(&parentTracker->def->property_structs.patrol_properties->patrol_points));

		// delete point
		StructDestroy(parse_WorldPatrolPointProperties, eaRemove(&parentTracker->def->property_structs.patrol_properties->patrol_points, pointsCopy[i]->childIdx));
		wleOpPropsEnd();
	}
	if (stack)
		EditUndoEndGroup(stack);
	zmapTrackerUpdate(NULL, false, false);

	if (verbose)
		emStatusPrintf("Deleted patrol points.");

	// handle UI changes
	wleOpRefreshUI();
	eaDestroy(&pointsCopy);
	return true;
}

bool wleOpDuplicatePatrolPoints(const WleEncObjSubHandle **points)
{
	const WleEncObjSubHandle **pointsCopy = NULL;
	WorldPatrolPointProperties **newPoints = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	bool validated = true;
	int i;

	// validate: something is being duplicated; otherwise, quietly return
	if (eaSize(&points) == 0)
		return false;

	for (i = 0; i < eaSize(&points); i++)
	{
		// validate: each of the trackers are modifiable
		if (!wleTrackerIsEditable(points[i]->parentHandle, false, true, true))
		{
			validated = false;
			break;
		}
	}

	if (!validated)
		return false;

	eaCopy(&pointsCopy, &points);
	eaQSort(pointsCopy, wleOpSubHandleCmp);
	if (stack)
		EditUndoBeginGroup(stack);

	edObjSelectionClear(EDTYPE_PATROL_POINT);
	for (i = 0; i < eaSize(&pointsCopy); i++)
	{
		WorldPatrolPointProperties *point;
		TrackerHandle *parentHandle = pointsCopy[i]->parentHandle;
		GroupTracker *parentTracker;

		parentTracker = wleOpPropsBegin(parentHandle);
		assert(parentTracker && parentTracker->def && parentTracker->def->property_structs.patrol_properties && pointsCopy[i]->childIdx >= 0 && pointsCopy[i]->childIdx < eaSize(&parentTracker->def->property_structs.patrol_properties->patrol_points));

		// duplicate point
		point = StructClone(parse_WorldPatrolPointProperties, parentTracker->def->property_structs.patrol_properties->patrol_points[pointsCopy[i]->childIdx]);
		eaInsert(&parentTracker->def->property_structs.patrol_properties->patrol_points, point, pointsCopy[i]->childIdx+1);
		eaPush(&newPoints, point);
		wleOpPropsEnd();
	}
	for (i = 0; i < eaSize(&pointsCopy); i++)
	{
		GroupTracker *parentTracker = trackerFromTrackerHandle(pointsCopy[i]->parentHandle);
		int childIdx;
		
		assert(parentTracker && parentTracker->def && parentTracker->def->property_structs.patrol_properties);
		childIdx = eaFind(&parentTracker->def->property_structs.patrol_properties->patrol_points, newPoints[i]);
		assert(childIdx >= 0);
		wlePatrolPointSelect(pointsCopy[i]->parentHandle, childIdx, true, false);
	}

	if (stack)
		EditUndoEndGroup(stack);
	zmapTrackerUpdate(NULL, false, false);

	// handle UI changes
	wleOpRefreshUI();
	eaDestroy(&newPoints);
	eaDestroy(&pointsCopy);
	return true;
}

int wleOpAddEncounterActor(const TrackerHandle *encounter, int index, Mat4 worldMat)
{
	EditUndoStack *stack = edObjGetUndoStack();
	WorldEncounterProperties *encounterProps = NULL;
	WorldActorProperties *encounterActor;
	GroupTracker *tracker = trackerFromTrackerHandle(encounter);
	Mat4 tempMat, invMat;
	bool validated = true;
	char name[32];
	int retIdx = -1;
	int iActorNum = 1;

	// validate: user is allowed to edit encounter
	if (!wleTrackerIsEditable(encounter, false, true, true))
		validated = false;

	if (!validated)
		return retIdx;

	tracker = wleOpPropsBegin(encounter);
	assert(tracker && tracker->def && (encounterProps = tracker->def->property_structs.encounter_properties));

	wleTrackerDeselectAll();

	encounterActor = StructCreate(parse_WorldActorProperties);
	trackerGetMat(tracker, tempMat);
	invertMat4(tempMat, invMat);
	mulMat4(invMat, worldMat, tempMat);
	getMat3YPR(tempMat, encounterActor->vRot);
	copyVec3(tempMat[3], encounterActor->vPos);
	if (index < 0)
		eaPush(&encounterProps->eaActors, encounterActor);
	else
		eaInsert(&encounterProps->eaActors, encounterActor, index);

	// find a unique name for an actor
	while(true)
	{
		int j;
		sprintf(name, "Actor_%i", iActorNum);
		for(j=eaSize(&encounterProps->eaActors)-1; j>=0; --j)
		{
			if (stricmp(name, encounterProps->eaActors[j]->pcName) == 0) 
				break;
		}
		if (j < 0)
			break;
		++iActorNum;
	}
	encounterActor->pcName = StructAllocString(name);

	// select the new actor
	retIdx = index < 0 ? eaSize(&encounterProps->eaActors) - 1 : index;
	wleEncounterActorSelect(encounter, retIdx, false, true);
	wleOpPropsEnd();

	return retIdx;
}

bool wleOpMoveEncounterActors(const WleEncObjSubHandle **actors, Mat4 *worldMats)
{
	EditUndoStack *stack = edObjGetUndoStack();
	Mat4 tempMat, invMat;
	bool validated = true;
	int i;

	// validate: something is being moved; otherwise, quietly return
	if (eaSize(&actors) == 0)
		return false;

	for (i = 0; i < eaSize(&actors); i++)
	{
		// validate: each of the trackers are modifiable
		if (!wleTrackerIsEditable(actors[i]->parentHandle, false, true, true))
			validated = false;
	}

	if (!validated)
		return false;

	if (stack)
		EditUndoBeginGroup(stack);

	for (i = 0; i < eaSize(&actors); i++)
	{
		TrackerHandle *parentHandle = actors[i]->parentHandle;
		GroupTracker *parentTracker;
		WorldActorProperties *encounterActor = NULL;

		parentTracker = wleOpPropsBegin(parentHandle);
		if (parentTracker && parentTracker->def && parentTracker->def->property_structs.encounter_properties && actors[i]->childIdx >= 0 && actors[i]->childIdx < eaSize(&parentTracker->def->property_structs.encounter_properties->eaActors))
			encounterActor = parentTracker->def->property_structs.encounter_properties->eaActors[actors[i]->childIdx];
		assert(encounterActor);

		// move actor
		trackerGetMat(parentTracker, tempMat);
		invertMat4(tempMat, invMat);
		mulMat4(invMat, worldMats[i], tempMat);
		getMat3YPR(tempMat, encounterActor->vRot);
		copyVec3(tempMat[3], encounterActor->vPos);

		wleOpPropsEnd();
	}
	if (stack)
		EditUndoEndGroup(stack);

	// handle UI changes
	wleOpRefreshUI();
	return true;
}

bool wleOpMoveEncounterActorToIndex(const WleEncObjSubHandle *actor, int index)
{
	EditUndoStack *stack = edObjGetUndoStack();
	TrackerHandle *parentHandle = actor->parentHandle;
	GroupTracker *parentTracker;
	WorldActorProperties *actorProps = NULL;
	ZoneMapLayer *parentLayer;
	bool validated = true;

	// validate: actor's parent tracker is modifiable
	if (!wleTrackerIsEditable(parentHandle, false, true, true))
		validated = false;

	if (!validated)
		return false;

	if (stack)
		EditUndoBeginGroup(stack);

	edObjSelectionClear(EDTYPE_ENCOUNTER_ACTOR);
	parentTracker = wleOpPropsBegin(parentHandle);

	// move the actor to appropriate index
	assert(parentTracker && parentTracker->def && parentTracker->def->property_structs.encounter_properties && actor->childIdx >= 0 && actor->childIdx < eaSize(&parentTracker->def->property_structs.encounter_properties->eaActors));
	parentLayer = parentTracker->parent_layer;
	eaMove(&parentTracker->def->property_structs.encounter_properties->eaActors, index, actor->childIdx); 
	wleOpPropsEnd();

	wleEncounterActorSelect(parentHandle, index, true, false);
	if (stack)
		EditUndoEndGroup(stack);

	// handle UI changes
	wleOpRefreshUI();
	return true;
}

bool wleOpDeleteEncounterActorsEx(const WleEncObjSubHandle **actors, bool verbose)
{
	const WleEncObjSubHandle **actorsCopy = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	bool validated = true;
	int i;

	// validate: something is being deleted; otherwise, quietly return
	if (eaSize(&actors) == 0)
		return false;

	for (i = 0; i < eaSize(&actors); i++)
	{
		// validate: each of the trackers are modifiable
		if (!wleTrackerIsEditable(actors[i]->parentHandle, false, true, true))
			validated = false;
	}

	if (!validated)
		return false;

	eaCopy(&actorsCopy, &actors);
	eaQSort(actorsCopy, wleOpSubHandleCmp);
	if (stack)
		EditUndoBeginGroup(stack);

	edObjSelectionClear(EDTYPE_ENCOUNTER_ACTOR);
	for (i = 0; i < eaSize(&actorsCopy); i++)
	{
		TrackerHandle *parentHandle = actorsCopy[i]->parentHandle;
		GroupTracker *parentTracker;

		parentTracker = wleOpPropsBegin(parentHandle);
		assert(parentTracker && parentTracker->def && parentTracker->def->property_structs.encounter_properties && actorsCopy[i]->childIdx >= 0 && actorsCopy[i]->childIdx < eaSize(&parentTracker->def->property_structs.encounter_properties->eaActors));

		// delete point
		StructDestroy(parse_WorldActorProperties, eaRemove(&parentTracker->def->property_structs.encounter_properties->eaActors, actorsCopy[i]->childIdx));
		wleOpPropsEnd();
	}
	if (stack)
		EditUndoEndGroup(stack);
	zmapTrackerUpdate(NULL, false, false);

	if (verbose)
		emStatusPrintf("Deleted patrol points.");

	// handle UI changes
	wleOpRefreshUI();
	eaDestroy(&actorsCopy);
	return true;
}

bool wleOpDuplicateEncounterActors(const WleEncObjSubHandle **actors)
{
	const WleEncObjSubHandle **actorsCopy = NULL;
	WorldActorProperties **newActors = NULL;
	EditUndoStack *stack = edObjGetUndoStack();
	bool validated = true;
	int i;

	// validate: something is being duplicated; otherwise, quietly return
	if (eaSize(&actors) == 0)
		return false;

	for (i = 0; i < eaSize(&actors); i++)
	{
		// validate: each of the trackers are modifiable
		if (!wleTrackerIsEditable(actors[i]->parentHandle, false, true, true))
		{
			validated = false;
			break;
		}
	}

	if (!validated)
		return false;

	eaCopy(&actorsCopy, &actors);
	eaQSort(actorsCopy, wleOpSubHandleCmp);
	if (stack)
		EditUndoBeginGroup(stack);

	edObjSelectionClear(EDTYPE_ENCOUNTER_ACTOR);
	for (i = 0; i < eaSize(&actorsCopy); i++)
	{
		WorldActorProperties *actor;
		TrackerHandle *parentHandle = actorsCopy[i]->parentHandle;
		GroupTracker *parentTracker;
		char name[128];

		parentTracker = wleOpPropsBegin(parentHandle);
		assert(parentTracker && parentTracker->def && parentTracker->def->property_structs.encounter_properties && actorsCopy[i]->childIdx >= 0 && actorsCopy[i]->childIdx < eaSize(&parentTracker->def->property_structs.encounter_properties->eaActors));

		// duplicate actor
		actor = StructClone(parse_WorldActorProperties, parentTracker->def->property_structs.encounter_properties->eaActors[actorsCopy[i]->childIdx]);
		sprintf(name, "%s_2", parentTracker->def->property_structs.encounter_properties->eaActors[actorsCopy[i]->childIdx]->pcName);
		actor->pcName = allocAddString(name);
		eaInsert(&parentTracker->def->property_structs.encounter_properties->eaActors, actor, actorsCopy[i]->childIdx);
		eaPush(&newActors, actor);
		wleOpPropsEnd();
	}
	for (i = 0; i < eaSize(&actorsCopy); i++)
	{
		GroupTracker *parentTracker = trackerFromTrackerHandle(actorsCopy[i]->parentHandle);
		int childIdx;

		assert(parentTracker && parentTracker->def && parentTracker->def->property_structs.encounter_properties);
		childIdx = eaFind(&parentTracker->def->property_structs.encounter_properties->eaActors, newActors[i]);
		assert(childIdx >= 0);
		wleEncounterActorSelect(actorsCopy[i]->parentHandle, childIdx, true, false);
	}

	if (stack)
		EditUndoEndGroup(stack);
	zmapTrackerUpdate(NULL, false, false);

	// handle UI changes
	wleOpRefreshUI();
	eaDestroy(&newActors);
	eaDestroy(&actorsCopy);
	return true;
}


/********************
* CLIENT-SIDE WORLDGRID/ZONEMAP OPERATIONS
********************/
typedef struct wleOpLayerAsyncData
{
	char relPath[MAX_PATH];
	const char *region;
	ZoneMapLayer *layer;
} wleOpLayerAsyncData;

bool wleOpNewLayerFinish(ZoneMapInfo *zminfo, bool success, wleOpLayerAsyncData *data)
{
	ZoneMapLayer *layer;

	if (success)
	{
		layer = zmapAddLayer(NULL, data->relPath, NULL, data->region);

		wleOpRefreshUI();

		// report
		emStatusPrintf("Created layer \"%s\".", data->relPath);
	}

	SAFE_FREE(data);
	return true;
}

/******
* This function tells the server to create a new layer.
* PARAMS:
*   fileName - the name of the file where the layer will be saved
*   regionName - string name of the region that will be assigned to the layer
******/
void wleOpNewLayer(const char *fileName, const char *regionName)
{
	wleOpLayerAsyncData *data = calloc(1, sizeof(wleOpLayerAsyncData));

	fileRelativePath(fileName, data->relPath);
	if(zmapGetLayerByName(NULL, data->relPath)) {
		Alertf("A layer with that name already exists, please choose a different name.");
		SAFE_FREE(data);
		return;
	}
	if (regionName && strlen(regionName) > 0 && strcmpi(regionName, "Default") != 0)
		data->region = allocAddString(regionName);

	wleOpLockZoneMap(wleOpNewLayerFinish, data);
	worldIncModTime();
}

bool wleOpImportLayerFinish(ZoneMapInfo *zminfo, bool success, wleOpLayerAsyncData *data)
{
	ZoneMapLayer *layer;

	if (success)
	{
		layer = zmapAddLayer(NULL, data->relPath, NULL, NULL);

		worldUpdateBounds(true, false);
		wleOpRefreshUI();

		// report
		emStatusPrintf("Importing layer \"%s\"...", data->relPath);
	}

	SAFE_FREE(data);
	return true;
}

/******
* This function tells the server to import an existing layer into the current zonemap.
* PARAMS:
*   layerName - string filename pointing to the layer to import
******/
void wleOpImportLayer(const char *layerName)
{
	char relPath[MAX_PATH];
	wleOpLayerAsyncData *data;

	fileRelativePath(layerName, relPath);

	// validate that specified layer does not already exist in the map
	if (zmapGetLayerByName(NULL, relPath))
	{
		emStatusPrintf("Layer \"%s\" is already on the map!", relPath);
		return;
	}

	data = calloc(1, sizeof(wleOpLayerAsyncData));
	strcpy(data->relPath, relPath);

	wleOpLockZoneMap(wleOpImportLayerFinish, data);
}

bool wleOpDeleteLayerFinish(ZoneMapInfo *zminfo, bool success, wleOpLayerAsyncData *data)
{
	int i;

	if (success)
	{
		worldUpdateBounds(true, false);

		for (i = 0; i < zmapGetLayerCount(NULL); i++)
		{
			if (zmapGetLayer(NULL, i) == data->layer)
			{
				zmapRemoveLayer(NULL, i);
				break;
			}
		}

		worldUpdateBounds(true, false);
		zmapTrackerUpdate(NULL, false, true);

		wleOpRefreshUI();

		// report
		emStatusPrintf("Deleted layer \"%s\"...", data->relPath);
	}
	SAFE_FREE(data);
	return true;
}

/******
* This function tells the server to delete a layer association from the zonemap.  This will
* not delete the layer file itself.
* PARAMS:
*   layerName - string layer filename of layer to delete
******/
void wleOpDeleteLayer(const char *layerName)
{
	char relPath[MAX_PATH];
	ZoneMapLayer *layer;
	wleOpLayerAsyncData *data;

	fileRelativePath(layerName, relPath);

	// validate: specified layer exists
	layer = zmapGetLayerByName(NULL, relPath);
	if (!layer)
	{
		emStatusPrintf("Layer \"%s\" does not belong to the map!", relPath);
		return;
	}

	// set layer to be unlocked on the client side to prevent further operations
	if (layerGetLocked(layer) > 0)
		layerSetLocked(layer, 0);

	EditUndoStackClear(edObjGetUndoStack());

	data = calloc(1, sizeof(wleOpLayerAsyncData));
	strcpy(data->relPath, relPath);
	data->layer = layer;

	wleOpLockZoneMap(wleOpDeleteLayerFinish, data);
}

typedef struct wleOpSetLayerRegionData
{
	char region_name[256];
	ZoneMapLayer **layers;
} wleOpSetLayerRegionData;

bool wleOpSetLayerRegionFinish(ZoneMapInfo *zminfo, bool success, wleOpSetLayerRegionData *data)
{
	int i;
	if (success)
	{
		for (i = 0; i < eaSize(&data->layers); i++)
		{
			layerSetWorldRegion(data->layers[i], data->region_name);
			zmapSetLayerRegion(NULL, data->layers[i], data->region_name);
		}

		worldUpdateBounds(true, false);
		wleOpRefreshUI();

		// report
		emStatusPrintf("Put layers into region \"%s\".", data->region_name);
	}

	eaDestroy(&data->layers);
	SAFE_FREE(data);
	return true;
}

/******
* This function tells the server to set the region for a layer.
* PARAMS:
*   layerFilenames - EArray of strings of layer filenames
*   regionName - the name of the region; NULL for default region
******/
void wleOpSetLayerRegion(const char **layerFilenames, const char *regionName)
{
	int i;
	wleOpSetLayerRegionData *data = calloc(1, sizeof(wleOpSetLayerRegionData));

	// fix up region name
	if (!regionName || strlen(regionName) == 0)
		regionName = "Default";

	strcpy(data->region_name, regionName);

	// weed out invalid layers
	for (i = 0; i < eaSize(&layerFilenames); i++)
	{
		ZoneMapLayer *layer = zmapGetLayerByName(NULL, layerFilenames[i]);
		const char *layerRegion;

		// validate: layer exists
		if (!layer)
			continue;

		// validate: layer is not already in region
		layerRegion = layerGetWorldRegionString(layer);
		if (!layerRegion || strlen(layerRegion) == 0)
			layerRegion = "Default";
		if (strcmpi(layerRegion, regionName) == 0)
			continue;

		eaPush(&data->layers, layer);
		/*if (!layerGetTracker(layer))
			emStatusPrintf("WARNING: Contents of \"%s\" will not be visible if moved between regions!  Make it viewable to fix this!", layerGetFilename(layer));*/
	}

	if (eaSize(&data->layers) == 0)
	{
		eaDestroy(&data->layers);
		SAFE_FREE(data);
		return;
	}

	wleOpLockZoneMap(wleOpSetLayerRegionFinish, data);
}

AUTO_STRUCT;
typedef struct WleOpSetRegionAsyncData
{
	char *regionName;
	SkyInfoGroup *skyGroup;
	WorldRegionType type;

	char *cubemapTexName;
	int	maxPets;
	bool bWorldGeoClustering;
	int vehicleRules;	AST(NAME(VehicleRules))
	bool bIndoorLighting;
} WleOpSetRegionAsyncData;

static bool wleOpSetRegionOverrideCubemapFinish(ZoneMapInfo *zminfo, bool success, WleOpSetRegionAsyncData *data)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(NULL, data->regionName, false);

	if (success && region && data->cubemapTexName[0])
	{
		zmapRegionSetOverrideCubeMap(NULL, region, allocAddString(data->cubemapTexName));

		// report
		emStatusPrintf("Set region \"%s\"'s override cubemap.", data->regionName ? data->regionName : "Default");
	}

	StructDestroy(parse_WleOpSetRegionAsyncData, data);
	return true;
}

void wleOpSetRegionOverrideCubemap(WorldRegion *region, const char *cubemapTexName)
{
	BasicTexture *texture = texFindAndFlag(cubemapTexName, false, WL_FOR_WORLD);
	WleOpSetRegionAsyncData *data;
	
	// validate: passed-in texture is a cubemap
	if (!texture || !texIsCubemap(texture))
	{
		emStatusPrintf("\"%s\" is not a usable cubemap texture.", cubemapTexName);
		return;
	}

	data = StructCreate(parse_WleOpSetRegionAsyncData);
	data->regionName = StructAllocString(worldRegionGetRegionName(region));
	data->cubemapTexName = StructAllocString(cubemapTexName);

	wleOpLockZoneMap(wleOpSetRegionOverrideCubemapFinish, data);
}

static bool wleOpSetRegionMaxPetsFinish(ZoneMapInfo *zminfo, bool success, WleOpSetRegionAsyncData *data)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(NULL, data->regionName, false);

	if (success && region)
	{
		zmapRegionSetAllowedPetsPerPlayer(NULL, region, data->maxPets);

		// report
		emStatusPrintf("Set region \"%s\"'s properties.", data->regionName ? data->regionName : "Default");
	}

	StructDestroy(parse_WleOpSetRegionAsyncData, data);
	return true;
}

static bool wleOpSetRegionVehicleRulesFinish(ZoneMapInfo *zminfo, bool success, WleOpSetRegionAsyncData *data)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(NULL, data->regionName, false);

	if (success && region)
	{
		zmapRegionSetVehicleRules(NULL, region, data->vehicleRules);

		emStatusPrintf("Set region \"%s\"'s properties.", data->regionName ? data->regionName : "Default");
	}

	StructDestroy(parse_WleOpSetRegionAsyncData, data);
	return true;
}

void wleOpSetRegionMaxPets(SA_PARAM_NN_VALID WorldRegion *region, int maxPets)
{
	WleOpSetRegionAsyncData *data = StructCreate(parse_WleOpSetRegionAsyncData);
	data->regionName = StructAllocString(worldRegionGetRegionName(region));
	data->maxPets = maxPets;

	wleOpLockZoneMap(wleOpSetRegionMaxPetsFinish, data);
}

void wleOpSetVehicleRulesChanged(SA_PARAM_NN_VALID WorldRegion *region, int eVehicleRules)
{
	WleOpSetRegionAsyncData *data = StructCreate(parse_WleOpSetRegionAsyncData);
	data->regionName = StructAllocString(worldRegionGetRegionName(region));
	data->vehicleRules = eVehicleRules;

	wleOpLockZoneMap(wleOpSetRegionVehicleRulesFinish, data);
}

static bool wleOpSetRegionSkyGroupFinish(ZoneMapInfo *zminfo, bool success, WleOpSetRegionAsyncData *data)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(NULL, data->regionName, false);

	if (success)
	{
		zmapRegionSetSkyGroup(NULL, region, data->skyGroup);
		data->skyGroup = NULL;

		// report
		emStatusPrintf("Set region \"%s\"'s skies.", data->regionName ? data->regionName : "Default");
	}

	StructDestroy(parse_WleOpSetRegionAsyncData, data);
	return true;
}

/******
* This function tells the server to set a SkyInfoGroup on a WorldRegion.
* PARAMS:
*   region - the WorldRegion
*   sky_group - the SkyInfoGroup
******/
void wleOpSetRegionSkyGroup(WorldRegion *region, SkyInfoGroup *skyGroup)
{
	WleOpSetRegionAsyncData *data = StructCreate(parse_WleOpSetRegionAsyncData);
	data->regionName = StructAllocString(worldRegionGetRegionName(region));
	data->skyGroup = StructClone(parse_SkyInfoGroup, skyGroup);

	wleOpLockZoneMap(wleOpSetRegionSkyGroupFinish, data);
}

static bool wleOpSetRegionTypeFinish(ZoneMapInfo *zminfo, bool success, WleOpSetRegionAsyncData *data)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(NULL, data->regionName, false);
	const char *typeString = StaticDefineIntRevLookup(WorldRegionTypeEnum, data->type);

	if (success)
	{
		zmapRegionSetType(NULL, region, data->type);

		// report
		emStatusPrintf("Setting region \"%s\"'s type to %s...", data->regionName ? data->regionName : "Default", typeString);
	}

	StructDestroy(parse_WleOpSetRegionAsyncData, data);
	return true;
}

/******
* This function tells the server to set the type on a WorldRegion.
* PARAMS:
*   region - the WorldRegion
*   type - the region type
******/
void wleOpSetRegionType(WorldRegion *region, WorldRegionType type)
{
	WleOpSetRegionAsyncData *data = StructCreate(parse_WleOpSetRegionAsyncData);
	data->regionName = StructAllocString(worldRegionGetRegionName(region));
	data->type = type;

	wleOpLockZoneMap(wleOpSetRegionTypeFinish, data);
}


static bool wleOpSetRegionClusterWorldGeoFinish(ZoneMapInfo *zminfo, bool success, WleOpSetRegionAsyncData *data)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(NULL, data->regionName, false);

	if (success)
	{
		zmapRegionSetWorldGeoClustering(NULL, region, data->bWorldGeoClustering);

		// report
		emStatusPrintf("Setting region \"%s\"'s geometry clustering to %s...", data->regionName ? data->regionName : "Default", data->bWorldGeoClustering ? "on" : "off");
	}

	StructDestroy(parse_WleOpSetRegionAsyncData, data);
	return true;
}

/******
* This function tells the server to set the cluster geometry option on a WorldRegion.
* PARAMS:
*   region - the WorldRegion
*   bWorldGeoClustering - true indicates enable clustering; false indicates disable it
******/
void wleOpSetRegionClusterWorldGeo(WorldRegion *region, bool bWorldGeoClustering)
{
	WleOpSetRegionAsyncData *data = StructCreate(parse_WleOpSetRegionAsyncData);
	data->regionName = StructAllocString(worldRegionGetRegionName(region));
	data->bWorldGeoClustering = bWorldGeoClustering;

	wleOpLockZoneMap(wleOpSetRegionClusterWorldGeoFinish, data);
}

static bool wleOpSetRegionIndoorLightingFinish(ZoneMapInfo *zminfo, bool success, WleOpSetRegionAsyncData *data)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(NULL, data->regionName, false);

	if (success)
	{
		zmapRegionSetIndoorLighting(NULL, region, data->bIndoorLighting);

		// report
		emStatusPrintf("Setting region \"%s\"'s indoor lighting to %d...", data->regionName ? data->regionName : "Default", data->bIndoorLighting);
	}

	StructDestroy(parse_WleOpSetRegionAsyncData, data);
	return true;
}

/******
* This function tells the server to set the indoor lighting on a WorldRegion.
* PARAMS:
*   region - the WorldRegion
*   bIndoorLighting - the indoor lighting setting
******/
void wleOpSetRegionIndoorLighting(WorldRegion *region, bool bIndoorLighting)
{
	WleOpSetRegionAsyncData *data = StructCreate(parse_WleOpSetRegionAsyncData);
	data->regionName = StructAllocString(worldRegionGetRegionName(region));
	data->bIndoorLighting = bIndoorLighting;

	wleOpLockZoneMap(wleOpSetRegionIndoorLightingFinish, data);
}


typedef struct wleOpSetMapParamsAsyncData
{
	char public_name[256];
	ZoneMapType map_type;
	char queue_def[256];
	char pvp_type[256];
	U32 level;
	U32 force_team_size;
	EncounterDifficulty eDifficulty;
	ZoneRespawnType eRespawnType;
	U32 respawnTime;
} wleOpSetMapParamsAsyncData;

bool wleOpSetPublicNameFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetName(NULL, data->public_name);

		// report
		emStatusPrintf("Set map public name to %s.", data->public_name);
	}

	SAFE_FREE(data);
	return true;
}

/******
* This function tells the server to set the public name for the map.
* PARAMS:
*   publicName - the new public name
******/
void wleOpSetPublicName(const char *publicName)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	strcpy(data->public_name, publicName);

	wleOpLockZoneMap(wleOpSetPublicNameFinish, data);
}

bool wleOpSetMapTypeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetMapType(zminfo, data->map_type);

		// report
		emStatusPrintf("Set map type.");
	}

	SAFE_FREE(data);
	return true;
}

/******
* This function tells the server to set the type for the map.
* PARAMS:
*   mapType - the new map type
******/
void wleOpSetMapType(ZoneMapType mapType)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->map_type = mapType;

	wleOpLockZoneMap(wleOpSetMapTypeFinish, data);
}

bool wleOpSetMapLevelFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetMapLevel(zminfo, data->level);

		// report
		emStatusPrintf("Set map level.");
	}

	SAFE_FREE(data);
	return true;
}

bool wleOpSetMapDifficultyFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetMapDifficulty(zminfo, data->eDifficulty);

		// report
		emStatusPrintf("Set map difficulty.");
	}

	SAFE_FREE(data);
	return true;
}

bool wleOpSetMapForceTeamSizeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetMapForceTeamSize(zminfo, data->force_team_size);

		// report
		emStatusPrintf("Set map forced team size.");
	}

	SAFE_FREE(data);
	return true;
}

bool wleOpSetMapIgnoreTeamSizeBonusXPFinish(ZoneMapInfo *zminfo, bool success, void * bIgnoreTeamSizeBonusXP)
{
	if (success)
	{
		zmapInfoSetMapIgnoreTeamSizeBonusXP(zminfo, (bool)bIgnoreTeamSizeBonusXP);

		// report
		emStatusPrintf("Set map ignore team size bonus XP.");
	}

	wleUIMapPropertiesRefresh();

	return true;
}

bool wleOpSetMapUsedInUGCFinish(ZoneMapInfo *zminfo, bool success, void * bUsedInUGC)
{
	if (success)
	{
		zmapInfoSetMapUsedInUGC(zminfo, (bool)bUsedInUGC);

		// report
		emStatusPrintf("Set UsedInUGC.");
	}

	wleUIMapPropertiesRefresh();

	return true;
}

bool wleOpSetMapRespawnTypeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetRespawnType(zminfo,data->eRespawnType);

		emStatusPrintf("Set map respawn type.");
	}

	SAFE_FREE(data);
	return true;
}

static bool wleOpSetMapRespawnWaveTimeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetRespawnWaveTime(zminfo,data->respawnTime);

		emStatusPrintf("Set map wave respawn time.");
	}

	SAFE_FREE(data);
	return true;
}

static bool wleOpSetMapRespawnMinTimeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetRespawnMinTime(zminfo,data->respawnTime);

		emStatusPrintf("Set map min respawn time.");
	}

	SAFE_FREE(data);
	return true;
}

static bool wleOpSetMapRespawnMaxTimeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetRespawnMaxTime(zminfo,data->respawnTime);

		emStatusPrintf("Set map max respawn time.");
	}

	SAFE_FREE(data);
	return true;
}

static bool wleOpSetMapRespawnIncrementTimeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetRespawnIncrementTime(zminfo,data->respawnTime);

		emStatusPrintf("Set map increment respawn time.");
	}

	SAFE_FREE(data);
	return true;
}

static bool wleOpSetMapRespawnAttritionTimeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoSetRespawnAttritionTime(zminfo,data->respawnTime);

		emStatusPrintf("Set map attrition respawn time.");
	}

	SAFE_FREE(data);
	return true;
}

/******
* This function tells the server to set the level for the map.
* PARAMS:
*   level - the new map level
******/
void wleOpSetMapLevel(U32 level)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->level = level;

	wleOpLockZoneMap(wleOpSetMapLevelFinish, data);
}

/******
* This function tells the server to set the difficulty for the map.
* PARAMS:
*   difficulty - the new map difficulty
******/
void wleOpSetMapDifficulty(EncounterDifficulty eDifficulty)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->eDifficulty = eDifficulty;

	wleOpLockZoneMap(wleOpSetMapDifficultyFinish, data);
}

/******
* This function tells the server to set the respawn type for the map.
* PARAMS:
*   respawnType - the new respawn type
******/
void wleOpSetMapRespawnType(ZoneRespawnType eRespawnType)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->eRespawnType = eRespawnType;

	wleOpLockZoneMap(wleOpSetMapRespawnTypeFinish, data);
}

void wleOpSetMapRespawnWaveTime(U32 respawn_time)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->respawnTime = respawn_time;

	wleOpLockZoneMap(wleOpSetMapRespawnWaveTimeFinish, data);
}

void wleOpSetMapRespawnMinTime(U32 respawn_time)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->respawnTime = respawn_time;

	wleOpLockZoneMap(wleOpSetMapRespawnMinTimeFinish, data);
}

void wleOpSetMapRespawnMaxTime(U32 respawn_time)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->respawnTime = respawn_time;

	wleOpLockZoneMap(wleOpSetMapRespawnMaxTimeFinish, data);
}

void wleOpSetMapRespawnIncrementTime(U32 respawn_time)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->respawnTime = respawn_time;

	wleOpLockZoneMap(wleOpSetMapRespawnIncrementTimeFinish, data);
}

void wleOpSetMapRespawnAttritionTime(U32 respawn_time)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->respawnTime = respawn_time;

	wleOpLockZoneMap(wleOpSetMapRespawnAttritionTimeFinish, data);
}

bool welOpSetDefaultQueueDefFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *pData)
{
	if(success)
	{
		zmapInfoSetDefaultQueueDef(zminfo, pData->queue_def);

		emStatusPrintf("Set Default QueueDef.");
	}

	return true;
}

bool welOpSetDefaultPVPGameTypeFinish(ZoneMapInfo *zminfo, bool success, wleOpSetMapParamsAsyncData *pData)
{
	if(success)
	{
		zmapInfoSetDefaultPVPGameType(zminfo, pData->pvp_type);

		emStatusPrintf("Set Default Game Type.");
	}

	return true;
}

void wleUISetDefaultQueueDef(const char *pchQueueDef)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	strcpy(data->queue_def,pchQueueDef);

	wleOpLockZoneMap(welOpSetDefaultQueueDefFinish, data);
}

void wleUISetPVPGameType(const char *pchGameType)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	strcpy(data->pvp_type,pchGameType);

	wleOpLockZoneMap(welOpSetDefaultPVPGameTypeFinish, data);
}

void wleOpSetMapForceTeamSize(U32 force_team_size)
{
	wleOpSetMapParamsAsyncData *data = calloc(1, sizeof(wleOpSetMapParamsAsyncData));
	data->force_team_size = force_team_size;

	wleOpLockZoneMap(wleOpSetMapForceTeamSizeFinish, data);
}

void wleOpSetMapIgnoreTeamSizeBonusXP(bool bIgnoreTeamSizeBonusXP)
{
#pragma warning(suppress:4306) // 'type cast' : conversion from 'bool' to 'void *' of greater size
	wleOpLockZoneMap(wleOpSetMapIgnoreTeamSizeBonusXPFinish, (void *)bIgnoreTeamSizeBonusXP);
}

void wleOpSetMapUsedInUGC(bool bUsedInUGC)
{
#pragma warning(suppress:4306) // 'type cast' : conversion from 'bool' to 'void *' of greater size
	wleOpLockZoneMap(wleOpSetMapUsedInUGCFinish, (void *)bUsedInUGC);
}

bool wleOpSetMapDisplayNameFinish(ZoneMapInfo *zminfo, bool success, Message *newMsg)
{
	if (success)
	{
		zmapInfoSetDisplayNameMessage(zminfo, newMsg);

		emStatusPrintf("Set map display name.");
	}

	StructDestroy(parse_Message, newMsg);
	return true;
}

void wleOpSetMapDisplayName(const Message *message)
{
	Message *newMsg = StructClone(parse_Message, message);
	wleOpLockZoneMap(wleOpSetMapDisplayNameFinish, newMsg);
}

static bool wleOpSetMapPrivacyFinish(ZoneMapInfo *zminfo, bool success, char *privacy)
{
	char *nextId;
	char *last = NULL;

	zmapInfoClearPrivacy(zminfo);
	nextId = strtok_r(privacy, ",", &last);
	while (nextId)
	{
		zmapInfoAddPrivacy(zminfo, nextId);
		nextId = strtok_r(NULL, ",", &last);
	}

	wleUIMapPropertiesRefresh();
	SAFE_FREE(privacy);
	return true;
}

static bool wleOpSetMapParentMapNameFinish(ZoneMapInfo *zminfo, bool success, const char *parentMapName)
{
	if (success)
	{
		if (parentMapName) {
			if( !zminfo->pParentMap ) {
				zminfo->pParentMap = StructCreate( parse_ParentZoneMap );
			}
			zminfo->pParentMap->pchMapName = allocAddString(parentMapName);
		} else if (zminfo->pParentMap) {
			zminfo->pParentMap->pchMapName = NULL;
			if (!zminfo->pParentMap->pchSpawnPoint) {
				StructDestroySafe( parse_ParentZoneMap, &zminfo->pParentMap);
			}
		}
		zminfo->mod_time++;
	}

	return true;
}

void wleOpSetMapParentMapName(const char *parentMapName)
{
	if (stricmp_safe(parentMapName, zmapInfoGetParentMapName(NULL)) == 0)
		return;
	
	wleOpLockZoneMap(wleOpSetMapParentMapNameFinish, (char*)parentMapName);
}

static bool wleOpSetMapParentMapSpawnFinish(ZoneMapInfo *zminfo, bool success, const char *parentMapSpawn)
{
	if (success)
	{
		if (parentMapSpawn) {
			if( !zminfo->pParentMap ) {
				zminfo->pParentMap = StructCreate( parse_ParentZoneMap );
			}
			StructCopyString(&zminfo->pParentMap->pchSpawnPoint, parentMapSpawn);
		} else if (zminfo->pParentMap) {
			StructFreeStringSafe(&zminfo->pParentMap->pchSpawnPoint);
			if (!zminfo->pParentMap->pchMapName) {
				StructDestroySafe( parse_ParentZoneMap, &zminfo->pParentMap);
			}
		}
		zminfo->mod_time++;
	}

	return true;
}

void wleOpSetMapParentMapSpawn(const char *parentMapSpawn)
{
	if (stricmp_safe(parentMapSpawn, zmapInfoGetParentMapSpawnPoint(NULL)) == 0)
		return;
	
	wleOpLockZoneMap(wleOpSetMapParentMapSpawnFinish, (char*)parentMapSpawn);
}

static bool wleOpSetMapStartSpawnFinish(ZoneMapInfo *zminfo, bool success, const char *startSpawn)
{
	if (success)
	{
		zmapInfoSetStartSpawnName(zminfo, allocAddString(startSpawn));
	}

	return true;
} 

void wleOpSetMapStartSpawn(const char *startSpawn)
{
	if (stricmp_safe(startSpawn, zmapInfoGetStartSpawnName(NULL)) == 0)
		return;

	wleOpLockZoneMap(wleOpSetMapStartSpawnFinish, (char*)startSpawn);
}

static bool wleOpSetMapRewardTableFinish(ZoneMapInfo *zminfo, bool success, const char *rewardTableKey)
{
	if (success)
		zmapInfoSetRewardTable(zminfo, rewardTableKey);
	return true;
} 

static bool wleOpSetMapPlayerRewardTableFinish(ZoneMapInfo *zminfo, bool success, const char *rewardTableKey)
{
	if (success)
		zmapInfoSetPlayerRewardTable(zminfo, rewardTableKey);
	return true;
}

void wleOpSetMapRewardTable(const char *rewardTableKey)
{
	wleOpLockZoneMap(wleOpSetMapRewardTableFinish, (char*)rewardTableKey);
}

void wleOpSetMapPlayerRewardTable(const char *rewardTableKey)
{
	wleOpLockZoneMap(wleOpSetMapPlayerRewardTableFinish, (char*)rewardTableKey);
}

static bool wleOpSetMapRequiresExprFinish(ZoneMapInfo *zminfo, bool success, Expression *expr)
{
	if (success)
		zmapInfoSetRequiresExpr(zminfo, expr);
	exprDestroy(expr);
	return true;
} 

void wleOpSetMapRequiresExpr(Expression* expr)
{
	wleOpLockZoneMap(wleOpSetMapRequiresExprFinish, expr);
}

static bool wleOpSetMapPermissionExprFinish(ZoneMapInfo *zminfo, bool success, Expression *expr)
{
	if (success)
		zmapInfoSetPermissionExpr(zminfo, expr);
	exprDestroy(expr);
	return true;
} 

void wleOpSetMapPermissionExpr(Expression* expr)
{
	wleOpLockZoneMap(wleOpSetMapPermissionExprFinish, expr);
}

static bool wleOpSetMapRequiredClassCategorySetFinish(ZoneMapInfo *zminfo, bool success, const char *categorySetKey)
{
	if (success)
		zmapInfoSetRequiredClassCategorySet(zminfo, categorySetKey);
	return true;
} 

void wleOpSetMapRequiredClassCategorySet(const char* categorySetKey)
{
	wleOpLockZoneMap(wleOpSetMapRequiredClassCategorySetFinish,  (char*)categorySetKey);
}

static bool wleOpSetMapMastermindDefFinish(ZoneMapInfo *zminfo, bool success, const char *mastermindDefKey)
{
	if (success)
		zmapInfoSetMastermindDef(zminfo, mastermindDefKey);
	return true;
} 

void wleOpSetMapMastermindDef(const char *mastermindDefKey)
{
	wleOpLockZoneMap(wleOpSetMapMastermindDefFinish, (char*)mastermindDefKey);
}

static bool wleOpSetMapCivilianMapDefFinish(ZoneMapInfo *zminfo, bool success, const char *mastermindDefKey)
{
	if (success)
		zmapInfoSetCivilianMapDef(zminfo, mastermindDefKey);
	return true;
} 

void wleOpSetMapCivilianMapDef(const char *civilianMapDefKey)
{
	wleOpLockZoneMap(wleOpSetMapCivilianMapDefFinish, (char*)civilianMapDefKey);
}

static bool wleOpSetMapDisableVisitedTrackingFinish(ZoneMapInfo *zminfo, bool success, void *val)
{
	if (success)
		zmapInfoSetDisableVisitedTracking(zminfo, !!val);
	return true;
} 

void wleOpSetMapDisableVisitedTracking(bool disableVisitedTracking)
{
#pragma warning(suppress:4306) // 'type cast' : conversion from 'bool' to 'void *' of greater size
	wleOpLockZoneMap(wleOpSetMapDisableVisitedTrackingFinish, (void*) disableVisitedTracking);
}

/******
* This function sets who the map is private to; the input string should be a comma-delimited list of users.
* PARAMS:
*   privacy - string comma-delimited list of users to which the map's accessibility will be limited
******/
void wleOpSetMapPrivacy(const char *privacy)
{
	char *privacyCopy = strdup(privacy);
	wleOpLockZoneMap(wleOpSetMapPrivacyFinish, privacyCopy);
}

static bool wleOpAddVariableFinish(ZoneMapInfo *zminfo, bool success, WorldVariableDef *var)
{
	if (success)
	{
		zmapInfoAddVariableDef(zminfo, var);

		wleUIVariablePropertiesRefresh();
	}

	StructDestroy(parse_WorldVariableDef, var);

	return true;
}

/******
* This function creates a new variable on the zone map.
* PARAMS:
*   name - the variable name
*   type - the variable type
*   default_type - how the variable default value is determined
*   value - the specific default value (if default_type is WVARDEF_SPECIFY_DEFAULT)
******/
void wleOpAddVariable(WorldVariableDef *var)
{
	WorldVariableDef *dataCopy = StructClone(parse_WorldVariableDef, var);
	wleOpLockZoneMap(wleOpAddVariableFinish, dataCopy);
}

typedef struct wleOpVariableParamsAsyncData
{
	int index;
	WorldVariableDef *def;
} wleOpVariableParamsAsyncData;

static bool wleOpRemoveVariableFinish(ZoneMapInfo *zminfo, bool success, wleOpVariableParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoRemoveVariableDef(zminfo, data->index);

		wleUIVariablePropertiesRefresh();
	}

	SAFE_FREE(data);

	return true;
}

/******
* This function removes a variable on the zone map.
* PARAMS:
*   index - the index of the variable to remove
******/
void wleOpRemoveVariable(int index)
{
	wleOpVariableParamsAsyncData *data = calloc(1, sizeof(wleOpVariableParamsAsyncData));
	data->index = index;
	wleOpLockZoneMap(wleOpRemoveVariableFinish, data);
}

static bool wleOpModifyVariableFinish(ZoneMapInfo *zminfo, bool success, wleOpVariableParamsAsyncData *data)
{
	if (success)
	{
		zmapInfoModifyVariableDef(zminfo, data->index, data->def);

		wleUIVariablePropertiesRefresh();
	}

	StructDestroy(parse_WorldVariableDef, data->def);
	free(data);

	return true;
}

/******
* This function replaces the definition of a variable on the zonemap.
* PARAMS:
*   var - the new value for the variable definition
******/
void wleOpModifyVariable(int index, WorldVariableDef *var)
{
	wleOpVariableParamsAsyncData *data = calloc(1, sizeof(wleOpVariableParamsAsyncData));
	data->index = index;
	data->def = StructClone(parse_WorldVariableDef, var);

	wleOpLockZoneMap(wleOpModifyVariableFinish, data);
}

typedef struct wleOpGlobalGAELayerAsyncData
{
	int index;
	GlobalGAELayerDef *def;
} wleOpGlobalGAELayerAsyncData;

static bool wleOpAddGlobalGAELayerFinish(ZoneMapInfo *zminfo, bool success, GlobalGAELayerDef *var)
{
	if (success)
	{
		zmapInfoAddGAELayerDef(zminfo, var);

		wleUIGlobalGAELayersRefresh();
	}

	StructDestroy(parse_GlobalGAELayerDef, var);

	return true;
}

void wleOpAddGlobalGAELayer(GlobalGAELayerDef *var)
{
	GlobalGAELayerDef *dataCopy = StructClone(parse_GlobalGAELayerDef, var);
	wleOpLockZoneMap(wleOpAddGlobalGAELayerFinish, dataCopy);
}

static bool wleOpRemoveGlobalGAELayerFinish(ZoneMapInfo *zminfo, bool success, wleOpGlobalGAELayerAsyncData *data)
{
	if (success)
	{
		zmapInfoRemoveGAELayerDef(zminfo, data->index);

		wleUIGlobalGAELayersRefresh();
	}

	SAFE_FREE(data);

	return true;
}

void wleOpRemoveGlobalGAELayer(int index)
{
	wleOpGlobalGAELayerAsyncData *data = calloc(1, sizeof(wleOpGlobalGAELayerAsyncData));
	data->index = index;

	wleOpLockZoneMap(wleOpRemoveGlobalGAELayerFinish, data);
}


static bool wleOpModifyGlobalGAELayerFinish(ZoneMapInfo *zminfo, bool success, wleOpGlobalGAELayerAsyncData *data)
{
	if (success)
	{
		zmapInfoModifyGAELayerDef(zminfo, data->index, data->def);

		wleUIGlobalGAELayersRefresh();
	}

	StructDestroy(parse_GlobalGAELayerDef, data->def);
	free(data);

	return true;
}

void wleOpModifyGlobalGAELayer(int index, GlobalGAELayerDef *var)
{
	wleOpGlobalGAELayerAsyncData *data = calloc(1, sizeof(wleOpGlobalGAELayerAsyncData));
	data->index = index;
	data->def = StructClone(parse_GlobalGAELayerDef, var);

	wleOpLockZoneMap(wleOpModifyGlobalGAELayerFinish, data);
}



/******
* Regenerates Genesis maps.
******/
GenesisRuntimeStatus *wleOpGenesisRegenerate(bool seed_layout, bool seed_detail)
{
	static U32 genesis_last_regen_time = 0;
	if (wlGetFrameCount() <= genesis_last_regen_time+5)
	{
		return NULL;
	}
	else
	{
		GenesisRuntimeStatus *ret = genesisReseedMapDesc(PARTITION_CLIENT, worldGetActiveMap(), seed_layout, seed_detail, NULL);
		genesis_last_regen_time = wlGetFrameCount();
		
		wleOpRefreshUI();
		return ret;
	}
}

/******
* Sets Genesis Edit type maps.
******/

static bool wleOpGenesisSetEditTypeCB(ZoneMapInfo *zminfo, bool success, void *unused)
{
	if(success)
		zmapSetGenesisEditType(NULL, GENESIS_EDIT_EDITING);
	return true;
}

void wleOpGenesisSetEditType(GenesisEditType type)
{
	switch(type)
	{
	case GENESIS_EDIT_EDITING: 
		if(zmapLocked(NULL))
			zmapSetGenesisEditType(NULL, GENESIS_EDIT_EDITING);
		else
			wleOpLockZoneMap(wleOpGenesisSetEditTypeCB, NULL);
		break;
	case GENESIS_EDIT_STREAMING:
		if(zmapGetGenesisEditType(NULL) != GENESIS_EDIT_STREAMING)
			globCmdParse("InitMap");
		break;
	}
}


/******
* SERVER-SIDE WORLDGRID OPERATIONS
******/
static Packet *wleOpCreatePacket(U32 *req_id)
{
	Packet *pak = NULL;

	if (*editState.link && linkConnected(*editState.link))
	{
		pak = pktCreate(*editState.link, editState.editCmd);
		pktSendBitsAuto(pak, ++editState.reqID);
		pktSendBitsAuto(pak, worldGetModTime());
		if (req_id)
			*req_id = editState.reqID;
	}

	return pak;
}

static U32 wleOpSendFileNames(ServerCommand com, const char **fileNames)
{
	U32 ret;
	Packet *pak = wleOpCreatePacket(&ret);
	int size = eaSize(&fileNames);
	int i;

	if (!pak)
		return 0;

	pktSendBitsPack(pak, 1, com);
	pktSendBitsAuto(pak, size);
	for (i = 0; i < size; i++)
		pktSendString(pak, fileNames[i]);
	pktSend(&pak);
	return ret;
}

UIStatusWindow *new_zonemap_progress_window = NULL;

static bool wleOpNewZoneMapLoaded(ServerResponseStatus status, void *userdata, int step, int total_steps)
{
	if (new_zonemap_progress_window) {
		ui_StatusWindowClose(new_zonemap_progress_window);
		new_zonemap_progress_window = NULL;
	}
	return true;
}

void wleOpNewZoneMapComplete(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, char *zmapname)
{
	ZoneMapInfo *info = (ZoneMapInfo*)pResource;
	if (info && !stricmp(zmapInfoGetPublicName(info), zmapname))
	{
		if(worldIsZoneMapInNamespace(info)) {
			Sleep(3000);
			globCmdParsef("mapmove %s", zmapname);			
			wleOpNewZoneMapLoaded(0, NULL, 0, 0);
			zmapInfoRemoveUpdateCallback(wleOpNewZoneMapComplete);
		} else {
			Packet *pak;
			U32 req_id;

			pak = wleOpCreatePacket(&req_id);
			SAFE_FREE(zmapname);

			if (!pak)
				return;

			pktSendBitsPack(pak, 1, CommandOpenMap);
			pktSendString(pak, zmapInfoGetPublicName(info));
			pktSend(&pak);

			editLibCreateAsyncOp(req_id, wleOpNewZoneMapLoaded, NULL);

			// report and reset character position
			globCmdParse("setpos 10 100 10");
			zmapInfoRemoveUpdateCallback(wleOpNewZoneMapComplete);
		}
	}
}

void wleOpNewZoneMapTimedOut(void *unused, void *unused2)
{
	Errorf("Fatal Error: New Zone Map Timed Out");
	new_zonemap_progress_window = NULL;
}

/******
* This function tells the server to create a new zonemap and default layers.
* PARAMS:
*   dir - the relative path to the directory to create
*   fileName - the name of the zonemap file without extension; it will be used
*              to create the zonemap and layer files as appropriate
*   width - the width of the zone map (the number of 256 foot long blocks)
*   length - the length of the zone map (the number of 256 foot long blocks)
*   world_type - determines what type of default layers to create
*   zmap_type - server type of map to create
*   displayName - visible name of the new map, or NULL
*   publicName - public name of the new map, or NULL, which will autogenerate it
*   createSubfolder - bool indicating whether to automatically create a subfolder
*                     with all of the map contents
******/
void wleOpNewZoneMap(const char *dir, const char *fileName, int width, int length, WleUINewMapType world_type, ZoneMapType zmap_type, Message *displayName, const char *publicName, bool createSubfolder)
{
	char zonefilename[MAX_PATH];
	char relpath[MAX_PATH];
	ZoneMapInfo *zminfo;
	ZoneMap *zmap;
	char *c;
	char zoneName[MAX_PATH];

	// Validate that the path is within the data directory
	if (!fileIsInDataDirs(dir))
		return;

	fileRelativePath(dir, relpath);
	if (createSubfolder)
	{
		strcatf(relpath, "/%s", fileName);
		if (c = strrchr(relpath, '.'))
			*c = '\0';
	}

	// Construct the file paths
	sprintf(zonefilename, "%s/%s", relpath, fileName);
		
	if (publicName)
		strcpy(zoneName, publicName);
	else
	{
		strcpy(zoneName, fileName);
		if (c = strrchr(zoneName, '.'))
			*c = '\0';
	}

	// Create the ZoneMapInfo structure
	zminfo = zmapInfoNew(zonefilename, zoneName);

	if (displayName)
		zmapInfoSetDisplayNameMessage(zminfo, displayName);

	zmapInfoSetMapType(zminfo, (zmap_type == ZMTYPE_UNSPECIFIED ? ZMTYPE_MISSION : zmap_type));

	if (strStartsWith(zonefilename, "maps/TestMaps/"))
		zmapInfoAddPrivacy(zminfo, NULL); // make private to current user

	// Create genesis data or layers
	if (world_type == WLEUI_NEW_GENESIS_MAP)
	{
		zmapInfoAddGenesisData(zminfo, 0);
	}
	else
	{
		zmapInfoAddLayer(zminfo, "Default.layer", NULL);
	}

	worldResetWorldGrid();
	
	// Load the map to edit it
	if (!(zmap = zmapLoad(zminfo)))
	{
		return;
	}

	eaPush(&world_grid.maps, zmap);
	eafPush3(&world_grid.map_offsets, zerovec3);

	worldSetActiveMap(zmap);

	if (world_type == WLEUI_NEW_OUTDOOR_MAP && zmapGetLayer(zmap, 0) != NULL)
	{
		char layer_filename[CRYPTIC_MAX_PATH];
		IVec2 block_begin;
		IVec2 block_end;
		IVec2 terrain_begin = { 0, 0 };
		IVec2 terrain_end = { terrain_begin[0] + width - 1, terrain_begin[1] + length - 1 };
		ZoneMapLayer *layer = zmapGetLayer(zmap, 0);

		layerLoadStreaming(layer, zmap, "Default");
		if (!terrainCheckoutLayer(layer, NULL, 0, false))
		{
			return;
		}

		// Set the terrain editable
		layerSetMode(layer, LAYER_MODE_TERRAIN, false, false, false);
		layer->layer_mode = LAYER_MODE_EDITABLE;

		// Add the terrain blocks
		for (block_begin[1] = terrain_begin[1]; block_begin[1] <= terrain_end[1]; block_begin[1] += 5)
			for (block_begin[0] = terrain_begin[0]; block_begin[0] <= terrain_end[0]; block_begin[0] += 5)
			{
				StashTable heightmaps = stashTableCreateInt(128);
				block_end[0] = MIN(terrain_end[0], block_begin[0]+4);
				block_end[1] = MIN(terrain_end[1], block_begin[1]+4);
				//printf("Creating block (%d,%d)-(%d,%d)\n", block_begin[0], block_begin[1], block_end[0], block_end[1]);
				layerTerrainAddBlock(layer, block_begin, block_end);
			}

		initTerrainBlocks(layer, false);
		updateTerrainBlocks(layer);
		layerUpdateBounds(layer);
		layerTrackerUpdate(layer, true, false);

		fileLocateWrite(layerGetFilename(layer), layer_filename);
		mkdirtree(layer_filename);
		layerSave(layer, true, false);
	}
	else if (world_type == WLEUI_NEW_INDOOR_MAP && zmapGetLayer(zmap, 0) != NULL)
	{
		char layer_filename[CRYPTIC_MAX_PATH];
		ZoneMapLayer *layer = zmapGetLayer(zmap, 0);

		layerLoadStreaming(layer, zmap, "Default");
		if (!terrainCheckoutLayer(layer, NULL, 0, false))
		{
			return;
		}

		// Set the terrain editable
		layerSetMode(layer, LAYER_MODE_TERRAIN, false, false, false);
		layer->layer_mode = LAYER_MODE_EDITABLE;

		fileLocateWrite(layerGetFilename(layer), layer_filename);
		mkdirtree(layer_filename);
		layerSave(layer, true, false);
	}

	emStatusPrintf("Creating \"%s\"...", zonefilename);

	// Create a progress window if one doesn't exist
	if (!new_zonemap_progress_window) 
		new_zonemap_progress_window = ui_StatusWindow("Creating new map", "Creating...", 120, wleOpNewZoneMapTimedOut, NULL);

	zmapInfoSetUpdateCallback(wleOpNewZoneMapComplete, strdup(zmapInfoGetPublicName(zminfo)));

	// Send the ZoneMapInfo to the server for saving
	zmapInfoSave(zminfo);

	// Clear the current state & wait for the server to load the ZoneMap
	EditUndoStackClear(edObjGetUndoStack());
	worldLoadEmptyMap();

	//StructDestroy(parse_ZoneMapInfo, zminfo);
}

/******
* This function sends a packet to the server instructing it to load an existing ZoneMap.
* PARAMS:
*   dir - the relative path to the directory to load
*   fileName - the relative path filename of the ZoneMap
******/
void wleOpOpenZoneMap(const char *dir, const char *fileName)
{
	char tempName[MAX_PATH];
	if(dir && strStartsWith(dir, NAMESPACE_PATH)) {
		ZoneMapInfo *zmapInfo = StructCreate(parse_ZoneMapInfo);
		sprintf(tempName, "%s/%s", dir, fileName);
		if(ParserLoadSingleDictionaryStruct(tempName, g_ZoneMapDictionary, zmapInfo, 0)) {
			globCmdParsef("mapmove %s", zmapInfo->map_name);
		}
		StructDestroy(parse_ZoneMapInfo, zmapInfo);
	} else {
		Packet *pak = wleOpCreatePacket(NULL);

		if (!pak)
			return;

		EditUndoStackClear(edObjGetUndoStack());
		pktSendBitsPack(pak, 1, CommandOpenMap);
		if (dir)
			sprintf(tempName, "%s/%s", dir, fileName);
		else
			strcpy(tempName, fileName);
		pktSendString(pak, tempName);
		pktSend(&pak);

		// report and reset character position
		emStatusPrintf("Opening \"%s\"...", fileName);
		globCmdParse("setpos 10 100 10");
	}
}

UIStatusWindow *gSaveStatusWindow = NULL;
void wleOpSaveTimeoutCB(void *unused, void *unused2)
{
	Alertf("Save has timed out, your data has not been saved.");
	gSaveStatusWindow = NULL;
}

/******
* Handles the result from the saving operation.
******/
bool wleZoneMapSaveCallback(EMEditor *editor, const char *name, void *state_data, EMResourceState state, void *callback_data, bool success)
{
	ZoneMap *zmap = worldGetPrimaryMap();
	ZoneMapInfo *zminfo = zmapGetInfo(zmap);
	if (!stricmp(zmapInfoGetPublicName(zminfo), name))
	{
		if (state != EMRES_STATE_LOCK_SUCCEEDED)
		{
			emStatusPrintf("FAILED to save Zone Map %s.", name);
		}
		else
		{
			emStatusPrintf("Saved Zone Map %s.", name);
			zmapInfoSetSaved(zminfo);
			if(!zmapSaveLayers(zmap, NULL, false, true)) {
				emStatusPrintf("FAILED: Saving layers has failed.");
			} else if(zmapOrLayersUnsaved(NULL)) {			
				emStatusPrintf("FAILED: Layers or Zone Map still not saved.");
			} else {
				emStatusPrintf("Save Complete");		
			}
			wleOpRefreshUI();
		}
		ui_StatusWindowClose(gSaveStatusWindow);
		gSaveStatusWindow = NULL;
	}
	return true;
}

/******
* This function tells the server to save the current zone map.
******/
void wleOpSave(void)
{
	ZoneMapInfo *zminfo = zmapGetInfo(worldGetPrimaryMap());
	bool success = true;
	bool save_postponed = false;

	// report
	emStatusPrintf("Starting Save...");

	if(!objectLibrarySave(NULL)) {
		emStatusPrintf("FAILED: Object Library Save Failed");
		success = false;
	}

	if (zmapInfoGetUnsaved(zminfo) && !world_grid.needs_reload)
	{
		emSetResourceState(&worldEditor, zmapInfoGetPublicName(zminfo), EMRES_STATE_LOCKING_FOR_SAVE);
		zmapInfoSave(zminfo);
		save_postponed = true;
	}
	else
	{
		ZoneMap *zmap = worldGetPrimaryMap();

		if (zmapInfoGetUnsaved(zminfo)) {
			Errorf("The current zonemap has unreloaded changes on the server; zonemap saving is disabled until the zonemap is reloaded.");
			success = false;
		}
		if(!zmapSaveLayers(zmap, NULL, false, true)) {
			emStatusPrintf("FAILED: Saving layers has failed.");
			success = false;
		}
	}

	// Generate Genesis mission data
	genesisGenerateMissionsOnServer(zminfo);

	EditUndoStackClear(edObjGetUndoStack());
	wleUISearchClearUniqueness();

	wleNotesSave();

	wleOpRefreshUI();

	if(!save_postponed && zmapOrLayersUnsaved(NULL)) {
		emStatusPrintf("FAILED: Layers or Zone Map still not saved.");
		success = false;
	}

	if(!save_postponed && success) {
		emStatusPrintf("Save Complete");		
	}

	if(save_postponed) {
		if(gSaveStatusWindow)
			ui_StatusWindowClose(gSaveStatusWindow);
		gSaveStatusWindow = ui_StatusWindow("Please Wait", "Saving...", 30, wleOpSaveTimeoutCB, NULL);
	}
}

/******
* This function tells the server to reload the current zone map from source files.
******/
void wleOpReloadFromSource(void)
{
	Packet *pak = wleOpCreatePacket(NULL);

	if (!pak)
		return;

	EditUndoStackClear(edObjGetUndoStack());
	pktSendBitsPack(pak, 1, CommandReloadFromSource);
	pktSend(&pak);

	// report
	emStatusPrintf("Reloading from source...");
}

/******
* This function tells the server to initiate a UGC map publish.
******/
void wleOpUGCPublish(void)
{
	Packet *pak = wleOpCreatePacket(NULL);

	if (!pak)
		return;

	EditUndoStackClear(edObjGetUndoStack());
	pktSendBitsPack(pak, 1, CommandUGCPublish);
	pktSend(&pak);

	// report
	emStatusPrintf("Publishing...");
}

/******
* This function saves the current ZoneMap and its layers to another location.
* PARAMS:
*   dir - the relative path to the directory to save the new zonemap
*   fileName - the filename of the new zonemap
*   publicName - string public name to assign to the map
*   createSubfolder - bool indicating whether to put the layer and zone file in a subfolder
*                     with the same name as the zonemap
******/
void wleOpSaveZoneMapAs(const char *dir, const char *fileName, const char *publicName, bool createSubfolder, bool layersAsRefrence, bool keepExistingReferenceLayers)
{
	char relativePath[MAX_PATH];
	char tempName[MAX_PATH];
	char abspath[MAX_PATH];
	ZoneMap *zmap = worldGetPrimaryMap();
	ZoneMapInfo *zminfo;

	fileRelativePath(dir, relativePath);

	// construct the file path
	if (createSubfolder)
	{
		char subdirName[MAX_PATH];
		char *c;
		strcpy(subdirName, fileName);
		if (c = strrchr(subdirName, '.'))
			*c = '\0';
		strcat(relativePath, "/");
		strcat(relativePath, subdirName);
	}
	sprintf(tempName, "%s/%s", relativePath, fileName);
	fileLocateWrite(tempName, abspath);
	mkdirtree(abspath);

	// report
	emStatusPrintf("Saving map as \"%s\"...", tempName);

	// clear the stack before saving, as layers are going to get unloaded
	EditUndoStackClear(edObjGetUndoStack());

	if(!layersAsRefrence || !keepExistingReferenceLayers) {
		// Save copies of all the layers, but if we're saving layers as reference layers, then no need to save them since they are not being moved.
		zmapSaveLayersEx(zmap, tempName, true, false, keepExistingReferenceLayers);
	}

	// Create a copy of the ZoneMapInfo and save it
	zminfo = zmapInfoCopy(zmapGetInfo(zmap), tempName, publicName);
	zmapInfoSetUpdateCallback(wleOpNewZoneMapComplete, strdup(publicName));
	
	// Generate Genesis mission data
	genesisGenerateMissionsOnServer( zminfo );

	// Send the ZoneMapInfo to the server for saving
	zmapInfoSave(zminfo);

	wleNotesSaveAs(relativePath,fileName);
}

typedef struct LockFilesParams {
	char **fileNames;
	EditAsyncOpFunction callback;
	void *userdata;
} LockFilesParams;

/******
* This function tells the server to lock a set of layers.
* PARAMS:
*   fileNames - EArray of filenames of files to lock
******/
void wleOpLockFiles(const char **fileNames, EditAsyncOpFunction callback, void *userdata)
{
	U32 req_id;

	if(zmapInfoHasGenesisData(NULL))
	{
		Alertf("You cannot edit in an autogen map.");
		if (callback)
			callback(StatusError, userdata, 0, 0);
		return;
	}

	resSetDictionaryEditMode(gMessageDict, true);
	req_id = wleOpSendFileNames(CommandLock, fileNames);

	if (!!req_id && callback)
		editLibCreateAsyncOp(req_id, callback, userdata);
}

/******
* This function tells the server to lock a particular layer.
* PARAMS
*   fileName - filename of file to lock
******/
#endif
AUTO_COMMAND ACMD_CATEGORY(World, Debug) ACMD_NAME("Editor.DebugLockFile");
void wleOpLockFileDebug(const char *fileName)
{
#ifndef NO_EDITORS
	wleOpLockFile(fileName, NULL, NULL);
#endif
}
#ifndef NO_EDITORS

void wleOpLockFile(const char *fileName, EditAsyncOpFunction callback, void *userdata)
{
	const char **fileNames = NULL;
	eaPush(&fileNames, fileName);
	wleOpLockFiles(fileNames, callback, userdata);
	eaDestroy(&fileNames);
}

void wleOpCommitGenesisData()
{
	int i;
	Packet *pak;
	ZoneMap *zmap = worldGetActiveMap();
	GenesisRuntimeStatus *gen_status;

	wleClientSave(true);

	zmapSetGenesisViewType(zmap, GENESIS_VIEW_FULL);
	if(zmap->map_info.genesis_data)
		zmap->map_info.genesis_data->skip_terrain_update = false;

	gen_status = wleOpGenesisRegenerate(false, false);
	if (!gen_status)
		return;
	
	if (genesisStatusHasErrors(gen_status, 0))
	{
		char* message = NULL;
		estrConcatStatic(&message, "Generation has only warnings.  Do you want to freeze anyway?\n\nWarnings:");
		{
			int stageIt;
			for( stageIt = 0; stageIt != eaSize(&gen_status->stages); ++stageIt ) {
				int errorIt;
				for( errorIt = 0; errorIt != eaSize(&gen_status->stages[stageIt]->errors); ++errorIt ) {
					estrConcatf( &message, "\n%s", gen_status->stages[stageIt]->errors[errorIt]->message );
				}
			}
		}
		if (genesisStatusHasErrors(gen_status, GENESIS_FATAL_ERROR) || ui_ModalDialog("Genesis Generation Report", message, ColorBlack, UIYes | UINo ) == UINo)
		{
			wleGenesisDisplayErrorDialog(gen_status);
			estrDestroy(&message);
			StructDestroy(parse_GenesisRuntimeStatus, gen_status);
			return;
		}
		estrDestroy(&message);
	}

	wleGenesisDisplayErrorDialog(gen_status);
	StructDestroy(parse_GenesisRuntimeStatus, gen_status);

	terEdOncePerFrame(0.0f);
	terEdWaitForQueuedEvents(NULL);

	for (i = 0; i < zmapGetLayerCount(zmap); i++)
		if (!terrainCheckoutLayer(zmapGetLayer(zmap, i), NULL, 0, false))
		{
			Errorf("Cannot checkout layer file. Aborting.");
			return;
		}

	// save out a backup map description
	if (!zmapInfoBackupMapDesc( NULL )) {
		Errorf("Could not backup mapdescription.  Aborting");
		return;
	}
	zmapCommitGenesisData(NULL);
	
	wleOpSave();

	pak = wleOpCreatePacket(NULL); // TomY ENCOUNTER_HACK
	if (!pak)
		return;

	pktSendBitsPack(pak, 1, CommandSaveDummyEncounters);
	pktSend(&pak);
}

void wleOpRestoreGenesisData()
{
	ZoneMapInfo* zmapInfo = worldGetZoneMapByPublicName(zmapInfoGetPublicName(NULL));
	
	wleClientSave(true);

	{
		GenesisRuntimeStatus *gen_status = genesisUnfreeze(zmapInfo);
		wleGenesisDisplayErrorDialog(gen_status);
		StructDestroy(parse_GenesisRuntimeStatus, gen_status);
	}
	
	// Send the ZoneMapInfo to the server for saving
	zmapInfoSetUpdateCallback( wleOpNewZoneMapComplete, strdup( zmapInfoGetPublicName( zmapInfo )));
	zmapInfoSave( zmapInfo );

	// Clear the current state & wait for the server to load the ZoneMap
	EditUndoStackClear(edObjGetUndoStack());
	worldLoadEmptyMap();
}

/******
* This function tells the server to unlock a particular set of layers.
* PARAMS:
*   fileNames - EArray of filenames of files to lock
*   save - bool indicating whether to save (true) or revert (file) the file
******/
void wleOpUnlockFiles(const char **fileNames, bool save, EditAsyncOpFunction callback, void *userdata)
{
	ZoneMapLayer *layer;
	int i;
	U32 req_id;

	EditUndoStackClear(edObjGetUndoStack());
	req_id = wleOpSendFileNames(CommandUnlock, fileNames);

	if (!!req_id && callback)
		editLibCreateAsyncOp(req_id, callback, userdata);

	// we unlock the file on the client side immediately to ensure that the user doesn't
	// attempt changes while the server is acknowledging the unlock
	for (i = 0; i < eaSize(&fileNames); i++)
	{
		if (layer = zmapGetLayerByName(NULL, fileNames[i]))
			layerSetLocked(layer, 0);
	}
	wleOpRefreshUI();
}

/******
* This function tells the server to unlock a particular layer.
* PARAMS
*   fileName - filename of file to lock
******/
void wleOpUnlockFile(const char *fileName, EditAsyncOpFunction callback, void *userdata)
{
	const char **fileNames = NULL;
	eaPush(&fileNames, fileName);
	wleOpUnlockFiles(fileNames, false, callback, userdata);
	eaDestroy(&fileNames);
}

void wleOpSaveAndUnlockFile(const char *fileName, EditAsyncOpFunction callback, void *userdata)
{
	const char **fileNames = NULL;
	eaPush(&fileNames, fileName);
	wleOpUnlockFiles(fileNames, true, callback, userdata);
	eaDestroy(&fileNames);
}

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Debug) ACMD_NAME("Editor.DebugUnlockFile");
void wleOpUnlockFileDebug(const char *fileName)
{
#ifndef NO_EDITORS
	wleOpUnlockFile(fileName, NULL, NULL);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Debug) ACMD_NAME("Editor.DebugSaveUnlockFile");
void wleOpSaveAndUnlockFileDebug(const char *fileName)
{
#ifndef NO_EDITORS
	wleOpSaveAndUnlockFile(fileName, NULL, NULL);
#endif
}

/******
* This function sends a packet to the server instructing it to list the its locks with respect to
* the client that sent this command.
******/
AUTO_COMMAND ACMD_CATEGORY(World, Debug) ACMD_NAME("Editor.DebugListLocks");
void wleOpListLocks(void)
{
#ifndef NO_EDITORS
	Packet *pak = wleOpCreatePacket(NULL);
	if (!pak)
		return;
	pktSendBitsPack(pak, 1, CommandDebugListLocks);
	pktSend(&pak);
#endif
}
#ifndef NO_EDITORS

/******
* This function snaps a tracker's position downward if it hits terrain or geo.
* PARAMS:
*   handle - the TrackerHandle to the tracker to snap down
******/
void wleOpSnapDown(const TrackerHandle *handle)
{
	GroupTracker *tracker = NULL;
	TrackerHandle *parentHandle = NULL;

	assert((tracker = trackerFromTrackerHandle(handle)) && tracker->def);

	// validate: not snapping a layer
	if(!tracker->parent)
	{
		emStatusPrintf("Layers cannot be snapped!");
		return;
	}
	if(eaSize(&tracker->children))
	{
		emStatusPrintf("Non-leaf Trackers cannot be snapped! Please deselect Trackers that are not leaves.");
		return;
	}
	parentHandle = trackerHandleCreate(tracker->parent);
	assert(parentHandle);

	// validate: user is allowed to modify parent
	if(!wleTrackerIsEditable(parentHandle, false, true, true))
		return;

	{
		Mat4 world;
		WorldCollCollideResults results;
		bool bHit = false;
		Vec3 vStart, vEnd;
		WorldRegion *region = NULL;

		trackerGetMat(tracker, world);

		region = worldGetWorldRegionByPos(world[3]);
		if(region)
		{
			copyVec3(world[3], vStart);
			copyVec3(vStart, vEnd);
			vStart[1] = region->world_bounds.world_max[1];
			vEnd[1] = region->world_bounds.world_min[1];

			bHit = wcRayCollide(worldGetActiveColl(PARTITION_CLIENT), vStart, vEnd, WC_QUERY_BITS_WORLD_ALL, &results);
			if(bHit)
			{
				GroupTracker *parentTracker = NULL;
				Mat4 inverse, parent, parentInverse, result;
				EditUndoStack *stack = edObjGetUndoStack();

				EditUndoBeginGroup(stack);
				parentTracker = wleOpPropsBegin(parentHandle);
				assert(parentTracker);

				invertMat4(parentTracker->def->children[tracker->idx_in_parent]->mat, inverse);
				mulMat4(world, inverse, parent);
				invertMat4(parent, parentInverse);
			
				copyVec3(results.posWorldImpact, world[3]);

				mulMat4(parentInverse, world, result);
				copyMat4(result, parentTracker->def->children[tracker->idx_in_parent]->mat);

				wleOpPropsEnd();
				EditUndoEndGroup(stack);
			}
		}
	}

	trackerHandleDestroy(parentHandle);

	editState.trackerRefreshRequested = true;
}

#include "WorldEditorOperations_c_ast.c"

#endif
